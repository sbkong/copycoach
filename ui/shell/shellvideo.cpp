// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/media/videofile.h"
#include "core/model/timemap.h"
#include "core/project/project.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"
#include "ui/video/videopanel.h"

#include <QDockWidget>

// Video in the copy workspace (video.md): a video file is a source version whose audio is pulled into the
// project's cache (a MediaExtract job) and then played like any recording; the video panel shows the frame
// at the audio clock's time and is hidden for projects without video (states.md section 2 "숨김").
namespace cc::ui
{

using action::Id;

void Shell::wireVideoActions()
{
    m_actions->action(Id::VideoMirror)->setCheckable(true);
    connect(m_actions->action(Id::VideoMirror), &QAction::toggled, m_videoPanel, &VideoPanel::setMirrored);
    connect(m_actions->action(Id::VideoFramePrev), &QAction::triggered, this, [this] {
        m_videoPanel->stepFrame(-1);
    });
    connect(m_actions->action(Id::VideoFrameNext), &QAction::triggered, this, [this] {
        m_videoPanel->stepFrame(1);
    });
    connect(m_videoPanel, &VideoPanel::focusChanged, this, [this](bool focused) {
        m_actions->setFocusContext(focused ? workspace::Context::Video : workspace::Context::Global);
    });
    connect(m_videoPanel, &VideoPanel::failed, this, &Shell::showStatus);
    connect(m_videoPanel, &VideoPanel::seekRequested, this, [this](double seconds) {
        if (const model::TimeMap *map = m_audio->copyTimeMap(); map != nullptr && m_audio->copyMode()) {
            m_audio->seekToTick(map->secondsToTick(seconds));
        }
    });
    connect(m_videoPanel, &VideoPanel::audioTrackChosen, this, &Shell::chooseVideoAudioTrack);
    connect(m_audio, &AudioService::positionChanged, this, [this](std::int64_t ticks) {
        const model::TimeMap *map = m_audio->copyTimeMap();
        if (m_videoPanel->hasVideo() && map != nullptr && m_audio->copyMode()) {
            m_videoPanel->showAt(map->tickToSeconds(ticks));
        }
    });
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::updateVideoDock);
    connect(m_project, &ProjectSession::sourcesChanged, this, &Shell::updateVideoDock);
    updateVideoDock();
}

/// A video file as a source version; its audio is extracted before it can play.
void Shell::importVideo(const std::filesystem::path &file)
{
    const Result<model::Id> id = m_project->importVideo(file);
    if (!id) {
        showStatus(errorText(id.error()));
        return;
    }
    if (m_active != workspace::Id::Copy) {
        switchTo(workspace::Id::Copy);
    }
    showStatus(tr("영상 불러옴: %1 · 오디오 추출 중").arg(QString::fromStdWString(file.filename().wstring())));
    extractVideoAudio();
}

/// The MediaExtract job for the active video version; when it lands the recording plays and the waveform
/// shows (inline without a queue, as in tests).
void Shell::extractVideoAudio()
{
    const auto video = m_project->activeVideoPath();
    const auto flac = m_project->activeSourcePath();
    const auto source = m_project->activeSource();
    if (!video || !flac || !source) {
        return;
    }
    const model::Id id = source->id;
    const int track = source->audioTrack;
    const jobs::Work work = [video = *video, flac = *flac, track](const std::stop_token &stop,
                                                                  const jobs::Report &report) {
        return media::extractAudio(video, track, flac, stop, report);
    };
    const auto done = [this, id](const Result<void> &r) {
        if (!r) {
            showStatus(errorText(r.error()));
            return;
        }
        if (m_project->activeSourceId() == id) {
            pushCopyScore();
            refreshWaveform();
        }
    };
    if (m_jobs == nullptr) {
        const std::stop_source never;
        done(work(never.get_token(), [](double) { }));
        return;
    }
    const jobs::JobId jobId = m_jobs->enqueue(jobs::JobKind::MediaExtract, work);
    connect(m_jobs, &JobService::finished, this, [jobId, done](const jobs::JobEvent &event) {
        if (event.id == jobId) {
            done(event.error ? Result<void>(std::unexpected(*event.error)) : Result<void>());
        }
    });
}

/// Another audio stream of the same file: an undoable change of the version, then a fresh extraction.
void Shell::chooseVideoAudioTrack(int track)
{
    const auto source = m_project->activeSource();
    if (!source || source->audioTrack == track) {
        return;
    }
    if (!m_editor->runCommand(edit::setSourceAudioTrack(m_project->project(), source->id, track))) {
        return;
    }
    extractVideoAudio();
}

/// The dock and its menu item follow the active version: shown and enabled for video, hidden otherwise.
void Shell::updateVideoDock()
{
    const auto video = m_project->activeVideoPath();
    const auto source = m_project->activeSource();
    const bool isVideo = video.has_value() && source.has_value();
    if (isVideo) {
        if (!m_videoPanel->hasVideo() || m_videoSource != source->id) {
            m_videoSource = source->id;
            m_videoPanel->setVideo(*video, source->audioTrack);
        }
    } else if (m_videoPanel->hasVideo()) {
        m_videoSource.clear();
        m_videoPanel->clearVideo();
    }
    m_actions->action(Id::ViewDockVideo)->setEnabled(isVideo);
    const auto dock = m_docks.find(panel::Id::DockVideo);
    if (dock != m_docks.end()) {
        dock->second->setVisible(isVideo && m_active == workspace::Id::Copy);
    }
    syncDockActions();
}

} // namespace cc::ui
