// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/renderworker.h"

#include "core/media/audiofile.h"
#include "core/media/resampler.h"

#include <algorithm>
#include <array>
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
    for (std::vector<float> &channel : m_mix) {
        channel.resize(maxSourceFramesPerChunk);
    }
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
        stream->work.resize(static_cast<std::size_t>(maxSourceFramesPerChunk)
                            * static_cast<std::size_t>(info.channels));
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

void RenderWorker::setSpeed(double factor)
{
    const std::scoped_lock lock(m_mutex);
    m_pending.speed = std::clamp(factor, minSpeed, maxSpeed);
    m_pending.speedPitchChanged = true;
}

void RenderWorker::setPitch(double semitones)
{
    const std::scoped_lock lock(m_mutex);
    m_pending.pitch = std::clamp(semitones, -maxPitchSemitones, maxPitchSemitones);
    m_pending.speedPitchChanged = true;
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
    if (m_pending.speedPitchChanged) {
        applySpeedPitch();
        m_pending.speedPitchChanged = false;
    }
    if (m_pending.seekFrame >= 0) {
        seekStreams(m_pending.seekFrame);
        m_pending.seekFrame = -1;
        m_generation = m_ring->generation.fetch_add(1, std::memory_order_relaxed) + 1;
        m_popsAtSeek = m_ring->popped.load(std::memory_order_acquire);
        m_discardsAtSeek = m_ring->discarded.load(std::memory_order_acquire);
        m_pushedSinceSeek = 0;
        m_ended = false;
        m_flushRemaining = -1;
        m_carry = 0.0;
        if (m_stretcher) {
            Stretcher &stretcher = *m_stretcher;
            stretcher.reset();
        }
    }
}

/// Takes the pending speed / pitch (under the lock); entering the stretched path resets the stretcher.
void RenderWorker::applySpeedPitch()
{
    const bool wasActive = stretchActive();
    m_speed = m_pending.speed;
    m_pitch = m_pending.pitch;
    if (!stretchActive()) {
        return;
    }
    if (!m_stretcher) {
        m_stretcher = std::make_unique<Stretcher>(m_deviceRate);
    }
    m_stretcher->setPitchSemitones(m_pitch);
    if (!wasActive) {
        Stretcher &stretcher = *m_stretcher;
        stretcher.reset();
    }
}

bool RenderWorker::stretchActive() const
{
    return m_speed != 1.0 || m_pitch != 0.0;
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

/// Source frames the next chunk consumes: 64 at the recording's tempo, 64 x speed when stretching (fractions
/// carried over), never past the loop end; wraps the sources first when the end is reached.
int RenderWorker::framesForNextChunk()
{
    const bool looping = m_loopTo > m_loopFrom;
    if (looping && m_sourceFrame >= m_loopTo) {
        seekStreams(m_loopFrom);
    }
    int frames = rt::blockFrames;
    if (stretchActive()) {
        const double desired = (rt::blockFrames * m_speed) + m_carry;
        frames = std::clamp(static_cast<int>(std::floor(desired)), 1, maxSourceFramesPerChunk);
        m_carry = desired - frames;
    }
    if (!looping) {
        return frames;
    }
    return static_cast<int>(std::min<std::int64_t>(frames, m_loopTo - m_sourceFrame));
}

bool RenderWorker::renderChunk()
{
    const int frames = framesForNextChunk();
    rt::Chunk chunk;
    chunk.generation = m_generation;
    const int longest = mixSources(frames);
    if (longest == 0 && !flushTail(chunk, frames)) { // every source is out of audio and the stretcher is drained
        m_ended = true;
        chunk.sourceFrame = m_sourceFrame;
    } else if (longest > 0 && stretchActive()) {
        emitStretched(chunk, longest);
    } else if (longest > 0) {
        emitBypass(chunk, longest);
    }
    if (!m_ring->queue.push(chunk)) { // full of chunks the callback has not drained yet: try again later
        return false;
    }
    ++m_pushedSinceSeek;
    m_sourceFrame += longest;
    return longest > 0 || chunk.frames > 0;
}

/// Pulls and mixes every source into m_mix for up to `frames` source frames; returns the longest pull.
int RenderWorker::mixSources(int frames)
{
    for (std::vector<float> &channel : m_mix) {
        std::ranges::fill(channel, 0.0F);
    }
    int longest = 0;
    for (std::size_t i = 0; i < m_streams.size(); ++i) {
        Stream &stream = *m_streams[i];
        pullStream(stream, frames);
        mixStream(stream, m_controls[i], stream.produced);
        longest = std::max(longest, stream.produced);
    }
    return longest;
}

void RenderWorker::emitBypass(rt::Chunk &chunk, int frames)
{
    chunk.sourceFrame = m_sourceFrame;
    chunk.frames = frames;
    for (std::size_t ch = 0; ch < rt::stereoChannels; ++ch) {
        std::ranges::copy(std::span<const float>(m_mix[ch]).first(static_cast<std::size_t>(frames)),
                          chunk.samples[ch].begin());
    }
}

/// `inFrames` mixed source frames become round(inFrames / speed) output frames, at most one block.
void RenderWorker::emitStretched(rt::Chunk &chunk, int inFrames)
{
    const int outFrames = std::clamp(static_cast<int>(std::llround(inFrames / m_speed)), 1, rt::blockFrames);
    chunk.sourceFrame = stretchTag();
    chunk.frames = outFrames;
    m_stretcher->process(Stretcher::ConstPlanar{m_mix[0].data(), m_mix[1].data()},
                         inFrames,
                         Stretcher::Planar{chunk.samples[0].data(), chunk.samples[1].data()},
                         outFrames);
}

/// After the sources ended, the stretcher still holds its latency worth of audio: feed silence until it is out.
/// Returns false once there is nothing left to flush (or the stretcher is not in use).
bool RenderWorker::flushTail(rt::Chunk &chunk, int inFrames)
{
    if (!stretchActive()) {
        return false;
    }
    if (m_flushRemaining < 0) {
        m_flushRemaining =
            m_stretcher->outputLatency() + static_cast<int>(std::llround(m_stretcher->inputLatency() / m_speed));
    }
    if (m_flushRemaining <= 0) {
        return false;
    }
    emitStretched(chunk, inFrames); // m_mix is silent here
    m_flushRemaining -= chunk.frames;
    return true;
}

/// The source frame the listener hears at the start of the next stretched chunk: what was fed, minus the
/// stretcher's input latency, minus its output latency scaled back to source frames.
std::int64_t RenderWorker::stretchTag() const
{
    const std::int64_t lag = m_stretcher->inputLatency() + std::llround(m_stretcher->outputLatency() * m_speed);
    return std::max<std::int64_t>(0, m_sourceFrame - lag);
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

/// Adds the stream's frames into m_mix: mono goes to both channels, wider files use their first two.
void RenderWorker::mixStream(const Stream &stream, const Control &control, int frames)
{
    const bool audible = !control.muted && (!anyoneSolo() || control.solo);
    if (!audible || frames == 0) {
        return;
    }
    const auto per = static_cast<std::size_t>(stream.channels);
    const std::size_t rightOffset = stream.channels > 1 ? 1 : 0;
    const auto count = static_cast<std::size_t>(frames);
    for (std::size_t f = 0; f < count; ++f) {
        m_mix[0][f] += stream.work[f * per] * control.gain;
        m_mix[1][f] += stream.work[(f * per) + rightOffset] * control.gain;
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
