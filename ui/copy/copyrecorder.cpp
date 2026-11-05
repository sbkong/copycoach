// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/copyrecorder.h"

#include "core/project/project.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/projectsession.h"

#include <cmath>

namespace cc::ui
{

namespace
{

constexpr double percent = 100.0;

nlohmann::json orNull(const std::optional<model::Id> &id)
{
    return id ? nlohmann::json(*id) : nlohmann::json(nullptr);
}

} // namespace

CopyRecorder::CopyRecorder(stats::IStatsSink &sink, const ProjectSession &project, AudioService &audio, QObject *parent)
    : QObject(parent)
    , m_sink(&sink)
    , m_project(&project)
    , m_audio(&audio)
{
    connect(m_project, &ProjectSession::projectChanged, this, &CopyRecorder::onProjectChanged);
    connect(m_audio, &AudioService::stateChanged, this, &CopyRecorder::onStateChanged);
    connect(m_audio, &AudioService::sourceFrameChanged, this, &CopyRecorder::onSourceFrame);
}

void CopyRecorder::setLoop(std::optional<std::pair<std::size_t, std::size_t>> bars, std::optional<model::Id> rangeId)
{
    m_bars = std::move(bars);
    m_rangeId = std::move(rangeId);
}

void CopyRecorder::recordPredictShow(std::string_view kind)
{
    m_sink->record(
        {std::string(stats::event::copyPredictShow), {{"project_id", projectId()}, {"kind", std::string(kind)}}});
}

void CopyRecorder::recordPredictAccept(std::string_view kind, std::size_t count)
{
    m_sink->record({std::string(stats::event::copyPredictAccept),
                    {{"project_id", projectId()}, {"kind", std::string(kind)}, {"count", count}}});
}

void CopyRecorder::recordScoreImport(std::string_view format)
{
    m_sink->record(
        {std::string(stats::event::copyScoreImport), {{"project_id", projectId()}, {"format", std::string(format)}}});
}

/// Only playback of the recording counts as copy practice; synth playback of the score alone does not.
void CopyRecorder::onStateChanged(audio::EngineState state)
{
    const bool playing = state == audio::EngineState::Playing && m_audio->copyMode();
    if (playing == m_playing) {
        return;
    }
    m_playing = playing;
    if (playing) {
        recordPlay();
    } else {
        recordStop();
    }
}

/// A frame earlier than the last one while playing is the loop wrapping (or a seek back: counted alike).
void CopyRecorder::onSourceFrame(std::int64_t frame)
{
    if (m_playing && m_lastFrame >= 0 && frame < m_lastFrame) {
        ++m_repeats;
    }
    m_lastFrame = frame;
}

void CopyRecorder::onProjectChanged()
{
    setLoop(std::nullopt, std::nullopt);
    if (m_project->hasProject()) {
        m_sink->record({std::string(stats::event::copyProjectOpen), {{"project_id", projectId()}}});
    }
}

void CopyRecorder::recordPlay()
{
    m_startedAt = std::chrono::steady_clock::now();
    m_lastFrame = -1;
    m_repeats = 0;
    nlohmann::json sources = nlohmann::json::array();
    if (const auto active = m_project->activeSourceId()) {
        sources.push_back(*active);
    }
    nlohmann::json bars = nullptr;
    if (m_bars) {
        bars = {m_bars->first + 1, m_bars->second + 1}; // 1-based like the UI
    }
    m_sink->record({std::string(stats::event::copyLoopPlay),
                    {{"project_id", projectId()},
                     {"range_id", orNull(m_rangeId)},
                     {"bars", bars},
                     {"speed_pct", static_cast<int>(std::lround(m_audio->speed() * percent))},
                     {"sources", sources}}});
}

void CopyRecorder::recordStop()
{
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_startedAt);
    m_sink->record({std::string(stats::event::copyLoopStop),
                    {{"project_id", projectId()}, {"duration", seconds.count()}, {"repeats", m_repeats}}});
}

std::string CopyRecorder::projectId() const
{
    return m_project->hasProject() ? m_project->project().projectId : std::string();
}

} // namespace cc::ui
