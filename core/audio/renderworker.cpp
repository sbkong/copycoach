// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/renderworker.h"

#include "core/media/audiofile.h"
#include "core/media/resampler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>

namespace cc::audio
{

/// One source file with its decode buffer and resampler. `work` holds `frames` output frames (device rate,
/// file channel count, interleaved) after a successful pullStream().
struct RenderWorker::Stream {
    media::AudioFileReader reader;
    std::unique_ptr<media::Resampler> resampler; // nullptr when the file already runs at the device rate
    int channels = 0;
    double ratio = 1.0; // device rate / file rate
    std::vector<float> in; // decoded, interleaved
    std::size_t inPos = 0; // frames of `in` already consumed
    std::size_t inFilled = 0; // frames of `in` holding data
    bool fileDone = false; // the reader returned nothing more
    bool drained = false; // the resampler flushed its tail too
    std::vector<float> work;
    int produced = 0; // frames in `work`

    [[nodiscard]] std::int64_t lengthFrames() const
    {
        return std::llround(static_cast<double>(reader.info().frames) * ratio);
    }
};

namespace
{

constexpr auto idleSleep = std::chrono::milliseconds(5); // section 15.4: the worker polls, the callback never wakes it

std::span<float> framesOf(std::vector<float> &buffer, std::size_t from, std::size_t count, int channels)
{
    const auto per = static_cast<std::size_t>(channels);
    return std::span<float>(buffer).subspan(from * per, count * per);
}

} // namespace

RenderWorker::RenderWorker(rt::ChunkRing &ring, double deviceRate)
    : m_ring(&ring)
    , m_deviceRate(deviceRate)
    , m_fillTarget(std::llround(defaultFillSeconds * deviceRate))
{
}

RenderWorker::~RenderWorker()
{
    stop();
}

Result<void> RenderWorker::load(const std::vector<std::filesystem::path> &paths)
{
    std::vector<std::unique_ptr<Stream>> streams;
    for (const std::filesystem::path &path : paths) {
        auto stream = std::make_unique<Stream>();
        if (auto opened = stream->reader.open(path); !opened) {
            return std::unexpected(opened.error());
        }
        const media::AudioFileInfo &info = stream->reader.info();
        stream->channels = info.channels;
        stream->ratio = m_deviceRate / info.sampleRate;
        if (info.sampleRate != m_deviceRate) {
            stream->resampler = std::make_unique<media::Resampler>(info.channels);
        }
        stream->in.resize(static_cast<std::size_t>(readFrames) * static_cast<std::size_t>(info.channels));
        stream->work.resize(static_cast<std::size_t>(rt::blockFrames) * static_cast<std::size_t>(info.channels));
        streams.push_back(std::move(stream));
    }
    std::int64_t longest = 0;
    for (const std::unique_ptr<Stream> &stream : streams) {
        longest = std::max(longest, stream->lengthFrames());
    }
    m_lengthOut.store(longest, std::memory_order_release);
    m_countOut.store(streams.size(), std::memory_order_release);
    const std::scoped_lock lock(m_mutex);
    m_pending.controls.assign(streams.size(), Control{}); // gain / mute / solo may be set before the first fill
    m_pending.controlsChanged = true;
    m_pending.streams = std::move(streams);
    m_pending.replaceStreams = true;
    m_pending.seekFrame = 0;
    return {};
}

void RenderWorker::unload()
{
    const std::scoped_lock lock(m_mutex);
    m_pending.streams.clear();
    m_pending.replaceStreams = true;
    m_lengthOut.store(0, std::memory_order_release);
    m_countOut.store(0, std::memory_order_release);
}

void RenderWorker::seek(std::int64_t sourceFrame)
{
    const std::scoped_lock lock(m_mutex);
    m_pending.seekFrame = std::max<std::int64_t>(0, sourceFrame);
}

void RenderWorker::setLoop(std::int64_t fromFrame, std::int64_t toFrame)
{
    const std::scoped_lock lock(m_mutex);
    m_pending.loopFrom = fromFrame;
    m_pending.loopTo = toFrame;
    m_pending.loopChanged = true;
}

void RenderWorker::setGain(std::size_t source, float gain)
{
    const std::scoped_lock lock(m_mutex);
    if (source < m_pending.controls.size()) {
        m_pending.controls[source].gain = gain;
        m_pending.controlsChanged = true;
    }
}

void RenderWorker::setMute(std::size_t source, bool muted)
{
    const std::scoped_lock lock(m_mutex);
    if (source < m_pending.controls.size()) {
        m_pending.controls[source].muted = muted;
        m_pending.controlsChanged = true;
    }
}

void RenderWorker::setSolo(std::size_t source, bool solo)
{
    const std::scoped_lock lock(m_mutex);
    if (source < m_pending.controls.size()) {
        m_pending.controls[source].solo = solo;
        m_pending.controlsChanged = true;
    }
}

void RenderWorker::setFillTarget(std::int64_t frames)
{
    const std::scoped_lock lock(m_mutex);
    m_pending.fillTarget = frames;
}

void RenderWorker::start()
{
    if (!m_thread.joinable()) {
        m_thread = std::jthread([this](const std::stop_token &token) {
            run(token);
        });
    }
}

void RenderWorker::stop()
{
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void RenderWorker::run(const std::stop_token &token)
{
    while (!token.stop_requested()) {
        if (!fill()) {
            std::this_thread::sleep_for(idleSleep);
        }
    }
}

/// Moves what the GUI asked for into the worker's own state, between chunks.
void RenderWorker::applyPending()
{
    const std::scoped_lock lock(m_mutex);
    if (m_pending.replaceStreams) {
        m_streams = std::move(m_pending.streams);
        m_pending.streams.clear();
        m_pending.replaceStreams = false;
        m_ended = false;
    }
    if (m_pending.controlsChanged) {
        m_controls = m_pending.controls;
        m_pending.controlsChanged = false;
    }
    if (m_pending.loopChanged) {
        m_loopFrom = m_pending.loopFrom;
        m_loopTo = m_pending.loopTo;
        m_pending.loopChanged = false;
    }
    if (m_pending.fillTarget > 0) {
        m_fillTarget = m_pending.fillTarget;
        m_pending.fillTarget = 0;
    }
    if (m_pending.seekFrame >= 0) {
        seekStreams(m_pending.seekFrame);
        m_pending.seekFrame = -1;
        m_generation = m_ring->generation.fetch_add(1, std::memory_order_relaxed) + 1;
        m_popsAtSeek = m_ring->popped.load(std::memory_order_acquire);
        m_discardsAtSeek = m_ring->discarded.load(std::memory_order_acquire);
        m_pushedSinceSeek = 0;
        m_ended = false;
    }
}

/// Chunks of the current generation the ring still holds: what the worker pushed since the seek, minus what
/// the callback has played of them (its pops that were not discards of older chunks).
std::size_t RenderWorker::liveChunks() const
{
    const std::uint64_t pops = m_ring->popped.load(std::memory_order_acquire) - m_popsAtSeek;
    const std::uint64_t discards = m_ring->discarded.load(std::memory_order_acquire) - m_discardsAtSeek;
    const std::uint64_t playedCurrent = pops - discards;
    return static_cast<std::size_t>(m_pushedSinceSeek - std::min(m_pushedSinceSeek, playedCurrent));
}

bool RenderWorker::fill()
{
    applyPending();
    const auto targetChunks = static_cast<std::size_t>((m_fillTarget + rt::blockFrames - 1) / rt::blockFrames);
    bool rendered = false;
    while (!m_streams.empty() && !m_ended && liveChunks() < targetChunks) {
        if (m_ring->queue.size() + 1 >= rt::ChunkRing::capacity) {
            break; // full of chunks the callback has not drained yet (stale ones after a seek): later
        }
        if (!renderChunk()) {
            break;
        }
        rendered = true;
    }
    m_sourceOut.store(m_sourceFrame, std::memory_order_release);
    m_endedOut.store(m_ended, std::memory_order_release);
    return rendered;
}

/// Frames the next chunk may hold before the loop end; wraps the sources first when the end is reached.
int RenderWorker::framesForNextChunk()
{
    const bool looping = m_loopTo > m_loopFrom;
    if (looping && m_sourceFrame >= m_loopTo) {
        seekStreams(m_loopFrom);
    }
    if (!looping) {
        return rt::blockFrames;
    }
    return static_cast<int>(std::min<std::int64_t>(rt::blockFrames, m_loopTo - m_sourceFrame));
}

bool RenderWorker::renderChunk()
{
    const int frames = framesForNextChunk();
    rt::Chunk chunk;
    chunk.sourceFrame = m_sourceFrame;
    chunk.generation = m_generation;
    int longest = 0;
    for (std::size_t i = 0; i < m_streams.size(); ++i) {
        Stream &stream = *m_streams[i];
        pullStream(stream, frames);
        mixStream(stream, m_controls[i], chunk, stream.produced);
        longest = std::max(longest, stream.produced);
    }
    chunk.frames = longest;
    if (longest == 0) { // every source is out of audio
        m_ended = true;
    }
    if (!m_ring->queue.push(chunk)) { // full of chunks the callback has not drained yet: try again later
        return false;
    }
    ++m_pushedSinceSeek;
    m_sourceFrame += longest;
    return longest > 0;
}

void RenderWorker::seekStreams(std::int64_t sourceFrame)
{
    for (const std::unique_ptr<Stream> &stream : m_streams) {
        const auto fileFrame = std::llround(static_cast<double>(sourceFrame) / stream->ratio);
        static_cast<void>(stream->reader.seek(fileFrame)); // past the end: the next read returns nothing
        if (stream->resampler) {
            media::Resampler &resampler = *stream->resampler;
            resampler.reset();
        }
        stream->inPos = stream->inFilled = 0;
        stream->fileDone = stream->drained = false;
    }
    m_sourceFrame = sourceFrame;
}

/// Decodes the next block of the file into `in`. False when the file has nothing more.
bool RenderWorker::readMore(Stream &stream) // static
{
    if (stream.fileDone) {
        return false;
    }
    const std::int64_t got = stream.reader.read(stream.in);
    stream.inPos = 0;
    stream.inFilled = static_cast<std::size_t>(got);
    stream.fileDone = got == 0;
    return got > 0;
}

/// Fills `work` with up to `frames` frames at the device rate; sets `produced` (0 when the source ended).
bool RenderWorker::pullStream(Stream &stream, int frames) // static
{
    stream.produced = 0;
    while (stream.produced < frames && !stream.drained) {
        if (stream.inPos == stream.inFilled && !readMore(stream) && !stream.resampler) {
            stream.drained = true;
            break;
        }
        const auto want = static_cast<std::size_t>(frames - stream.produced);
        const std::size_t have = stream.inFilled - stream.inPos;
        if (!stream.resampler) {
            const std::size_t take = std::min(want, have);
            std::ranges::copy(
                framesOf(stream.in, stream.inPos, take, stream.channels),
                framesOf(stream.work, static_cast<std::size_t>(stream.produced), take, stream.channels).begin());
            stream.inPos += take;
            stream.produced += static_cast<int>(take);
            continue;
        }
        const auto progress = stream.resampler->process(
            framesOf(stream.in, stream.inPos, have, stream.channels),
            framesOf(stream.work, static_cast<std::size_t>(stream.produced), want, stream.channels),
            stream.ratio,
            stream.fileDone);
        if (!progress) {
            stream.drained = true;
            break;
        }
        stream.inPos += static_cast<std::size_t>(progress->inputUsed);
        stream.produced += static_cast<int>(progress->outputProduced);
        if (stream.fileDone && progress->outputProduced == 0) {
            stream.drained = true;
        }
    }
    return stream.produced > 0;
}

bool RenderWorker::anyoneSolo() const
{
    return std::ranges::any_of(m_controls, [](const Control &c) {
        return c.solo;
    });
}

/// Adds the stream's frames into the chunk: mono goes to both channels, wider files use their first two.
void RenderWorker::mixStream(const Stream &stream, const Control &control, rt::Chunk &chunk, int frames) const
{
    const bool audible = !control.muted && (!anyoneSolo() || control.solo);
    if (!audible || frames == 0) {
        return;
    }
    const auto per = static_cast<std::size_t>(stream.channels);
    const std::size_t rightOffset = stream.channels > 1 ? 1 : 0;
    const auto count = static_cast<std::size_t>(frames);
    for (std::size_t f = 0; f < count; ++f) {
        chunk.samples[0][f] += stream.work[f * per] * control.gain;
        chunk.samples[1][f] += stream.work[(f * per) + rightOffset] * control.gain;
    }
}

std::int64_t RenderWorker::sourceFrame() const
{
    return m_sourceOut.load(std::memory_order_acquire);
}

std::int64_t RenderWorker::lengthFrames() const
{
    return m_lengthOut.load(std::memory_order_acquire);
}

bool RenderWorker::ended() const
{
    return m_endedOut.load(std::memory_order_acquire);
}

std::size_t RenderWorker::sourceCount() const
{
    return m_countOut.load(std::memory_order_acquire);
}

} // namespace cc::audio
