// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/media/peaks.h"
#include "core/model/timemap.h"
#include "core/project/project.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/audiotrackview.h"
#include "ui/copy/copybottomdock.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"

#include <algorithm>
#include <memory>
#include <optional>

// The copy workspace's bottom dock: waveform peaks through the job queue, the bar grid from the score and
// the version's sync points, the playhead from the recording's frame, selection shared with the score,
// sync points tapped on the track or with copy.sync-point.add at the current playback time.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr std::size_t waveformBuckets = 4000;

} // namespace

void Shell::wireCopyDock()
{
    AudioTrackView *track = m_copyDock->audioTrack();
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::refreshWaveform);
    connect(m_project, &ProjectSession::sourcesChanged, this, &Shell::refreshWaveform);
    connect(m_editor, &ScoreEditor::scoreChanged, this, &Shell::refreshTrackTiming);
    connect(m_editor, &ScoreEditor::cursorChanged, this, [this] {
        m_copyDock->showBeat(m_editor->score(), m_editor->cursor());
    });
    connect(m_audio, &AudioService::sourceFrameChanged, this, [this, track](std::int64_t frame) {
        const bool show = m_audio->isPlaying() && m_audio->sampleRate() > 0.0;
        track->setPlayheadSeconds(show ? std::optional(static_cast<double>(frame) / m_audio->sampleRate())
                                       : std::nullopt);
    });
    connect(m_audio, &AudioService::stateChanged, this, [track](audio::EngineState state) {
        if (state != audio::EngineState::Playing) {
            track->setPlayheadSeconds(std::nullopt);
        }
    });
    connect(track, &AudioTrackView::measuresSelected, this, [this](std::size_t from, std::size_t to) {
        m_scoreView->setSelectedMeasures(std::pair{from, to});
    });
    connect(m_scoreView, &ScoreView::selectionChanged, this, [this, track] {
        track->setSelectedMeasures(m_scoreView->selectedMeasures());
    });
    connect(track, &AudioTrackView::seekRequested, this, [this](double seconds) {
        if (const model::TimeMap *map = m_audio->copyTimeMap(); map != nullptr && m_audio->copyMode()) {
            m_audio->seekToTick(map->secondsToTick(seconds));
        }
    });
    connect(track, &AudioTrackView::syncPointRequested, this, &Shell::addSyncPointAt);
    connect(m_actions->action(Id::CopySyncPointAdd), &QAction::triggered, this, [this] {
        if (m_audio->sampleRate() > 0.0 && m_audio->sourceFrame() >= 0) {
            addSyncPointAt(static_cast<double>(m_audio->sourceFrame()) / m_audio->sampleRate());
        }
    });
}

/// The grid of the track = the score's tempos pinned by the active version's sync points; none without one.
void Shell::refreshTrackTiming()
{
    AudioTrackView *track = m_copyDock->audioTrack();
    const auto sourceId = m_project->activeSourceId();
    if (!m_project->hasProject() || !sourceId) {
        track->clearTiming();
        return;
    }
    track->setTiming(m_editor->score(), project::syncPointsFor(m_project->project(), *sourceId));
}

/// Peaks of the active recording, computed on the job queue (inline when the shell has no queue, as in tests).
void Shell::refreshWaveform()
{
    AudioTrackView *track = m_copyDock->audioTrack();
    const auto path = m_project->activeSourcePath();
    refreshTrackTiming();
    if (!path) {
        track->setPeaks(std::nullopt);
        return;
    }
    const auto result = std::make_shared<std::optional<media::Peaks>>();
    const jobs::Work work = [path = *path, result](const std::stop_token &stop,
                                                   const jobs::Report &report) -> Result<void> {
        Result<media::Peaks> peaks = media::computePeaks(path, waveformBuckets, stop, report);
        if (!peaks) {
            return std::unexpected(peaks.error());
        }
        *result = std::move(*peaks);
        return {};
    };
    if (m_jobs == nullptr) {
        const std::stop_source never;
        static_cast<void>(work(never.get_token(), [](double) { }));
        track->setPeaks(*result);
        return;
    }
    const jobs::JobId id = m_jobs->enqueue(jobs::JobKind::Waveform, work);
    connect(m_jobs, &JobService::finished, track, [track, id, result](const jobs::JobEvent &event) {
        if (event.id == id && !event.error) {
            track->setPeaks(*result);
        }
    });
}

/// "A bar starts here": the bar is the one under the score cursor, the time is where the user pointed or
/// where playback is. The cursor then moves on so the next tap marks the next bar.
void Shell::addSyncPointAt(double seconds)
{
    const auto sourceId = m_project->activeSourceId();
    const auto cursor = m_editor->cursor();
    if (!sourceId || !cursor) {
        showStatus(tr("싱크 포인트는 음원과 악보 커서가 있어야 찍을 수 있습니다"));
        return;
    }
    const model::Tick tick = m_editor->score().measureStartTick(cursor->measure);
    std::vector<model::SyncPoint> points = project::syncPointsFor(m_project->project(), *sourceId);
    std::erase_if(points, [tick](const model::SyncPoint &p) {
        return p.tick == tick;
    });
    points.push_back(model::SyncPoint{tick, seconds});
    std::ranges::sort(points, {}, &model::SyncPoint::tick);
    if (!m_editor->runCommand(edit::setSyncPoints(m_project->project(), *sourceId, points))) {
        showStatus(tr("싱크 포인트가 앞뒤 순서와 맞지 않습니다 (마디 %1, %2 s)")
                       .arg(cursor->measure + 1)
                       .arg(seconds, 0, 'f', 2));
        return;
    }
    pushCopyScore();
    showStatus(tr("싱크 포인트: 마디 %1 = %2 s").arg(cursor->measure + 1).arg(seconds, 0, 'f', 2));
    if (cursor->measure + 1 < m_editor->score().measures.size()) {
        m_editor->setCursor(BeatLocation{cursor->measure + 1, cursor->track, cursor->voice, 0});
    }
}

} // namespace cc::ui
