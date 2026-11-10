// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/project/project.h"
#include "generated/settingkeys.h"
#include "ui/copy/audiotrackview.h"
#include "ui/copy/beatgrid.h"
#include "ui/copy/copybottomdock.h"
#include "ui/copy/copyrecorder.h"
#include "ui/copy/modelfiles.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"
#include "ui/shell/settings.h"

#include <QUrl>

#include <filesystem>
#include <functional>
#include <memory>
#include <utility>

// Beat prediction in the copy workspace (copy-spec.md section 4 "박 예측", states.md section 2): copy.predict.beats
// fetches the Beat This! files on first use (an inline offer, then a ModelDownload job; the chord engine
// shares both), runs the model as a Prediction job, and shows the downbeats as a grey grid on the audio
// track. copy.predict.accept turns them into sync points, one per bar; copy.predict.toggle hides the grid.
// A new recording clears every prediction.
namespace cc::ui
{

using action::Id;

void Shell::wirePredictActions()
{
    m_actions->action(Id::CopyPredictToggle)->setCheckable(true);
    connect(m_actions->action(Id::CopyPredictBeats), &QAction::triggered, this, &Shell::runBeatPrediction);
    connect(m_actions->action(Id::CopyPredictAccept), &QAction::triggered, this, &Shell::acceptShownPrediction);
    connect(m_actions->action(Id::CopyPredictToggle), &QAction::toggled, this, [this](bool on) {
        m_copyDock->audioTrack()->setPredictionVisible(on);
    });
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::clearBeatPrediction);
    connect(m_project, &ProjectSession::sourcesChanged, this, &Shell::clearBeatPrediction);
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::clearNotePrediction);
    connect(m_project, &ProjectSession::sourcesChanged, this, &Shell::clearNotePrediction);
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::clearChordPrediction);
    connect(m_project, &ProjectSession::sourcesChanged, this, &Shell::clearChordPrediction);
    updatePredictActions();
}

predict::BeatThisVariant Shell::beatModelVariant() const
{
    return m_settings->stringValue(setting::Key::CopyPredictBeatModel) == QStringLiteral("small")
        ? predict::BeatThisVariant::Small
        : predict::BeatThisVariant::Final;
}

/// With the files present the job starts at once; otherwise the user is asked before anything is fetched.
void Shell::runBeatPrediction()
{
    if (!m_project->activeSourcePath()) {
        showStatus(tr("박 예측은 음원이 있어야 합니다 (파일 › 불러오기…)"));
        return;
    }
    if (m_predicting) {
        showStatus(tr("박 예측이 이미 진행 중입니다"));
        return;
    }
    const auto needed = predict::beatThisFiles(beatModelVariant());
    const std::vector<predict::ModelFile> missing = missingModels(beatThisDir(*m_paths), needed);
    if (missing.empty()) {
        startBeatPrediction();
        return;
    }
    std::uint64_t bytes = 0;
    for (const predict::ModelFile &file : missing) {
        bytes += file.size;
    }
    constexpr std::uint64_t mebibyte = 1024ULL * 1024ULL;
    offerModelDownload({tr("박 예측 모델이 아직 없습니다 (Beat This!, %1 MB). 공식 가중치에서 변환한 파일을 "
                           "GitHub 릴리스에서 내려받을까요? 앱에는 동봉되지 않습니다")
                            .arg(bytes / mebibyte),
                        QUrl(QString::fromUtf8(predict::beatThisReleaseUrl.data(),
                                               static_cast<qsizetype>(predict::beatThisReleaseUrl.size()))),
                        beatThisDir(*m_paths),
                        missing,
                        [this] {
                            runBeatPrediction();
                        }});
}

/// states.md section 4 "모델 없음": the size, the source, and that nothing ships with the app.
void Shell::offerModelDownload(const ModelDownload &download)
{
    m_copyMessage->show(MessageKind::Notice,
                        download.offer,
                        {MessageAction{tr("내려받기"),
                                       [this, download] {
                                           m_copyMessage->clear();
                                           startModelDownload(download);
                                       }},
                         MessageAction{tr("나중에"), [this] {
                                           m_copyMessage->clear();
                                       }}});
}

/// The ModelDownload job from the release URL; the prediction starts by itself once the files are in.
void Shell::startModelDownload(const ModelDownload &download)
{
    if (m_jobs == nullptr) {
        showStatus(tr("작업 큐가 없어 내려받을 수 없습니다"));
        return;
    }
    const jobs::JobId id =
        m_jobs->enqueue(jobs::JobKind::ModelDownload, downloadModels(download.baseUrl, download.dir, download.missing));
    connect(m_jobs, &JobService::finished, this, [id, then = download.then](const jobs::JobEvent &event) {
        if (event.id == id && !event.error) {
            then();
        }
    });
}

/// The Prediction job: decode, log-mel, the model, post-processing; the result lands on the track when done.
void Shell::startBeatPrediction()
{
    const auto path = m_project->activeSourcePath();
    const auto sourceId = m_project->activeSourceId();
    if (!path || !sourceId) {
        return;
    }
    const predict::BeatThisFiles files = beatThisFiles(beatThisDir(*m_paths), beatModelVariant());
    const auto result = std::make_shared<std::optional<predict::BeatPrediction>>();
    const jobs::Work work = [audio = *path, files, result](const std::stop_token &stop,
                                                           const jobs::Report &report) -> Result<void> {
        Result<predict::BeatPrediction> prediction = predict::predictBeats(audio, files, stop, report);
        if (!prediction) {
            return std::unexpected(prediction.error());
        }
        *result = std::move(*prediction);
        return {};
    };
    if (m_jobs == nullptr) { // tests: inline
        const std::stop_source never;
        const Result<void> r = work(never.get_token(), [](double) { });
        if (r && *result) {
            showBeatPrediction(**result, *sourceId);
        } else if (!r) {
            showStatus(errorText(r.error()));
        }
        return;
    }
    m_predicting = true;
    updatePredictActions();
    const jobs::JobId id = m_jobs->enqueue(jobs::JobKind::Prediction, work);
    connect(m_jobs, &JobService::finished, this, [this, id, result, sourceId = *sourceId](const jobs::JobEvent &event) {
        if (event.id != id) {
            return;
        }
        m_predicting = false;
        updatePredictActions();
        if (!event.error && *result) {
            showBeatPrediction(**result, sourceId);
        }
    });
}

void Shell::showBeatPrediction(const predict::BeatPrediction &prediction, const model::Id &sourceId, bool fromCache)
{
    m_beatPrediction = prediction;
    m_beatPredictionSource = sourceId;
    m_shownPrediction = ShownPrediction::Beats;
    m_copyDock->audioTrack()->setPredictedDownbeats(prediction.downbeats);
    m_actions->action(Id::CopyPredictToggle)->setChecked(true);
    updatePredictActions();
    if (fromCache) {
        return;
    }
    savePredictionCache(sourceId);
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictShow("beats");
    }
    showStatus(tr("박 예측: 마디 %1 · 박 %2 · %3 — 카피 › 예측 받아들이기로 싱크 포인트를 만듭니다")
                   .arg(prediction.downbeats.size())
                   .arg(prediction.beats.size())
                   .arg(providerLabel()));
}

/// Downbeat k = bar k starts here (ui/copy/beatgrid.h); bars the score lacks are inserted first, then the
/// version's sync points are replaced in one undoable pair.
void Shell::acceptBeatPrediction()
{
    const auto sourceId = m_project->activeSourceId();
    if (!m_beatPrediction || !sourceId || *sourceId != m_beatPredictionSource) {
        showStatus(tr("받아들일 예측이 없습니다 (카피 › 박 예측 실행…)"));
        return;
    }
    const std::vector<double> downbeats = m_beatPrediction->downbeats;
    const std::size_t extra = measuresNeeded(m_editor->score(), downbeats);
    if (extra > 0) {
        model::IdGenerator ids;
        const std::size_t at = m_editor->score().measures.size();
        if (!m_editor->runCommand(edit::insertMeasures(m_project->project(), at, extra, ids))) {
            showStatus(tr("마디 %1개를 추가하지 못했습니다").arg(extra));
            return;
        }
    }
    const std::vector<model::SyncPoint> points = syncPointsFromDownbeats(m_editor->score(), downbeats);
    if (!m_editor->runCommand(edit::setSyncPoints(m_project->project(), *sourceId, points))) {
        showStatus(tr("예측한 다운비트가 순서대로가 아니라 싱크 포인트로 만들 수 없습니다"));
        return;
    }
    m_actions->action(Id::CopyPredictToggle)->setChecked(false);
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictAccept("beats", points.size());
    }
    pushCopyScore();
    refreshTrackTiming();
    showStatus(tr("싱크 포인트 %1개를 만들었습니다 (마디 %2개 추가)").arg(points.size()).arg(extra));
}

void Shell::clearBeatPrediction()
{
    m_beatPrediction.reset();
    m_beatPredictionSource.clear();
    if (m_shownPrediction == ShownPrediction::Beats) {
        m_shownPrediction = ShownPrediction::None;
    }
    m_copyDock->audioTrack()->setPredictedDownbeats({});
    updatePredictActions();
}

void Shell::updatePredictActions()
{
    const bool beats = m_beatPrediction.has_value();
    m_actions->action(Id::CopyPredictBeats)->setEnabled(!m_predicting);
    m_actions->action(Id::CopyPredictAccept)->setEnabled(m_shownPrediction != ShownPrediction::None);
    m_actions->action(Id::CopyPredictToggle)->setEnabled(beats);
}

/// The layer the user saw last is the one accept means (copy-spec.md section 4): the beat grid, the notes or
/// the chords.
void Shell::acceptShownPrediction()
{
    switch (m_shownPrediction) {
    case ShownPrediction::Notes:
        acceptNotePrediction();
        break;
    case ShownPrediction::Chords:
        acceptChordPrediction();
        break;
    case ShownPrediction::Beats:
    case ShownPrediction::None:
        acceptBeatPrediction();
        break;
    }
}

} // namespace cc::ui
