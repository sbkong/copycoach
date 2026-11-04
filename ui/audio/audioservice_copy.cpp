// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The copy half of AudioService (audio-engine.md section 15): render worker, sources, timing map, seek and loop
// on the source timeline, speed, pitch and stems. The routine half is audioservice.cpp.
#include "ui/audio/audioservice.h"

#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace cc::ui
{

namespace
{

Q_LOGGING_CATEGORY(logCopyAudio, "cc.audio.copy")

using audio::CommandType;

} // namespace

void AudioService::send(CommandType type, std::int64_t a, std::int64_t b, double value, const void *res)
{
    static_cast<void>(post(audio::Command{type, a, b, value, res}));
}

// ---- mode and sources --------------------------------------------------------------------------------------------

void AudioService::buildWorker()
{
    m_worker = std::make_unique<audio::RenderWorker>(m_engine->fileRing(), m_sampleRate);
}

/// After an engine rebuild: the sources again, the map at the new rate, mix, speed, pitch and mode.
void AudioService::resendCopyState()
{
    if (!m_sourcePaths.empty()) {
        if (const auto loaded = m_worker->load(m_sourcePaths); !loaded) {
            qCWarning(logCopyAudio) << "sources lost on reopen:" << QString::fromStdString(loaded.error().detail);
            m_sourcePaths.clear();
            m_stems.clear();
        }
    }
    rebuildTimeMap();
    resendStemControls();
    m_worker->setSpeed(m_speed);
    m_worker->setPitch(m_pitch);
    send(CommandType::SetFileMode, 0, m_copyMode ? 1 : 0);
    if (m_copyMode) {
        send(CommandType::SetSpeed, 0, 0, m_speed);
        startWorkerIfLoaded();
        seekToTick(0); // the new transport starts at tick 0: put the recording there too
    }
}

void AudioService::startWorkerIfLoaded()
{
    if (m_copyMode && !m_sourcePaths.empty()) {
        m_worker->start(); // no-op while running
    }
}

void AudioService::setCopyMode(bool on)
{
    if (on == m_copyMode) {
        return;
    }
    stop();
    m_copyMode = on;
    send(CommandType::SetFileMode, 0, on ? 1 : 0);
    send(CommandType::SwapTimeMap, 0, 0, 0.0, on ? m_timeMap.get() : nullptr);
    if (on) {
        send(CommandType::SetSpeed, 0, 0, m_speed);
        startWorkerIfLoaded();
        seekToTick(m_lastTicks); // aims the recording and the count-in tempo at the cursor
    } else {
        m_worker->stop();
        send(CommandType::SetSpeed, 0, 0, 1.0);
        send(CommandType::SetTempo, 0, 0, m_tempoBpm); // back to the routine clock
    }
    applyLoop();
}

bool AudioService::copyMode() const
{
    return m_copyMode;
}

Result<void> AudioService::loadSources(std::vector<std::filesystem::path> paths)
{
    if (const auto loaded = m_worker->load(paths); !loaded) {
        return loaded;
    }
    m_sourcePaths = std::move(paths);
    m_stems.assign(m_sourcePaths.size(), StemControl{});
    startWorkerIfLoaded();
    return {};
}

void AudioService::unloadSources()
{
    stop();
    m_worker->stop();
    m_worker->unload();
    m_sourcePaths.clear();
    m_stems.clear();
}

std::size_t AudioService::sourceCount() const
{
    return m_sourcePaths.size();
}

std::int64_t AudioService::sourceLengthFrames() const
{
    return m_worker ? m_worker->lengthFrames() : 0;
}

std::int64_t AudioService::sourceFrame() const
{
    return m_sourceFrame;
}

// ---- timing ------------------------------------------------------------------------------------------------------

void AudioService::setCopyTiming(const model::Score &score, std::vector<model::SyncPoint> points)
{
    if (const auto bad = model::firstInvalidSyncPoint(points)) {
        qCWarning(logCopyAudio) << "sync points ignored: out of order at index" << *bad;
        points.clear();
    }
    m_copyScore = score;
    m_syncPoints = std::move(points);
    rebuildTimeMap();
    setTimeSignature(score.initialTimeSig.count, score.initialTimeSig.unit);
    if (m_copyMode) {
        applyCopyLoop(); // the loop frames follow the new map
        postCopyTempo(m_lastTicks);
    }
}

const model::TimeMap *AudioService::copyTimeMap() const
{
    return m_timeMap.get();
}

void AudioService::rebuildTimeMap()
{
    std::unique_ptr<model::TimeMap> map;
    if (m_copyScore) {
        map =
            std::make_unique<model::TimeMap>(model::TimeMap::fromSyncPoints(*m_copyScore, m_syncPoints, m_sampleRate));
    }
    if (m_copyMode) {
        send(CommandType::SwapTimeMap, 0, 0, 0.0, map.get());
    }
    if (m_timeMap) {
        m_retiredMaps.push_back(std::move(m_timeMap)); // the callback may still read it until Swapped
    }
    m_timeMap = std::move(map);
}

std::int64_t AudioService::frameForTick(std::int64_t tick) const
{
    if (!m_timeMap) {
        return 0; // no timing yet: the recording plays from its start
    }
    return std::max<std::int64_t>(0, m_timeMap->tickToSample(tick)); // the score may start before the audio
}

/// The clock tempo at `tick` (map tempo times speed), so a count-in started there rings at the song's tempo
/// before the first chunk re-aims the clock.
void AudioService::postCopyTempo(std::int64_t tick)
{
    if (!m_timeMap) {
        return;
    }
    const double bpm = m_timeMap->segmentAtSample(m_timeMap->tickToSample(tick)).bpm * m_speed;
    send(CommandType::SetTempo, 0, 0, bpm);
}

void AudioService::seekToTick(std::int64_t tick)
{
    send(CommandType::Seek, tick);
    m_lastTicks = tick;
    if (!m_copyMode) {
        return;
    }
    postCopyTempo(tick);
    m_worker->seek(frameForTick(tick));
}

// ---- loop, speed, pitch, stems ------------------------------------------------------------------------------------

void AudioService::setCopyLoop(std::int64_t startTick, std::int64_t endTick)
{
    m_copyLoopStart = startTick;
    m_copyLoopEnd = endTick;
    if (m_copyMode) {
        applyCopyLoop();
    }
}

/// The engine loop stays off in copy mode: the worker wraps on the source timeline and the callback reports
/// LoopWrapped when the chunk tags go backwards (audio-engine.md section 15.3).
void AudioService::applyCopyLoop()
{
    send(CommandType::SetLoop, 0, 0);
    m_worker->setLoop(frameForTick(m_copyLoopStart), frameForTick(m_copyLoopEnd));
}

void AudioService::setSpeed(double factor)
{
    m_speed = std::clamp(factor, audio::RenderWorker::minSpeed, audio::RenderWorker::maxSpeed);
    m_worker->setSpeed(m_speed);
    if (m_copyMode) {
        send(CommandType::SetSpeed, 0, 0, m_speed);
        postCopyTempo(m_lastTicks);
    }
}

void AudioService::setPitch(double semitones)
{
    m_pitch = std::clamp(semitones, -audio::RenderWorker::maxPitchSemitones, audio::RenderWorker::maxPitchSemitones);
    m_worker->setPitch(m_pitch);
}

double AudioService::speed() const
{
    return m_speed;
}

double AudioService::pitch() const
{
    return m_pitch;
}

void AudioService::setStemGain(std::size_t stem, float gain)
{
    if (stem >= m_stems.size()) {
        return;
    }
    m_stems[stem].gain = gain;
    m_worker->setGain(stem, gain);
}

void AudioService::setStemMute(std::size_t stem, bool muted)
{
    if (stem >= m_stems.size()) {
        return;
    }
    m_stems[stem].muted = muted;
    m_worker->setMute(stem, muted);
}

void AudioService::setStemSolo(std::size_t stem, bool solo)
{
    if (stem >= m_stems.size()) {
        return;
    }
    m_stems[stem].solo = solo;
    m_worker->setSolo(stem, solo);
}

void AudioService::resendStemControls()
{
    for (std::size_t i = 0; i < m_stems.size(); ++i) {
        m_worker->setGain(i, m_stems[i].gain);
        m_worker->setMute(i, m_stems[i].muted);
        m_worker->setSolo(i, m_stems[i].solo);
    }
}

} // namespace cc::ui
