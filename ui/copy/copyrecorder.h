// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/command.h"
#include "core/model/id.h"
#include "core/stats/event.h"

#include <QObject>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// Turns the copy workspace's doings into practice events (copy-spec.md section 5 "기록 이벤트", stats.md
// section 4): the project opened, each playback of the recording as one play/stop pair with the loop it ran on
// and how often it wrapped (F-3: once per start, never per repeat), predictions shown and accepted, scores
// imported. The sink is all it knows about recording; the shell tells it what the engine's signals cannot.
namespace cc::ui
{

class AudioService;
class ProjectSession;

class CopyRecorder : public QObject
{
    Q_OBJECT
public:
    CopyRecorder(stats::IStatsSink &sink,
                 const ProjectSession &project,
                 AudioService &audio,
                 QObject *parent = nullptr);

    /// What the next play runs on: the bars (inclusive) and the saved range they came from, if any.
    void setLoop(std::optional<std::pair<std::size_t, std::size_t>> bars, std::optional<model::Id> rangeId);
    void recordPredictShow(std::string_view kind);
    void recordPredictAccept(std::string_view kind, std::size_t count);
    void recordScoreImport(std::string_view format);

    // The engine's signals, public so tests can drive them without a device.
    void onStateChanged(audio::EngineState state);
    void onSourceFrame(std::int64_t frame);
    void onProjectChanged();

private:
    void recordPlay();
    void recordStop();
    [[nodiscard]] std::string projectId() const;

    stats::IStatsSink *m_sink;
    const ProjectSession *m_project;
    AudioService *m_audio;
    std::optional<std::pair<std::size_t, std::size_t>> m_bars;
    std::optional<model::Id> m_rangeId;
    bool m_playing = false;
    std::chrono::steady_clock::time_point m_startedAt;
    std::int64_t m_lastFrame = -1;
    int m_repeats = 0;
};

} // namespace cc::ui
