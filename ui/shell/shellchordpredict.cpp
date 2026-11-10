// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/model/locate.h"
#include "core/model/timemap.h"
#include "core/predict/chordmini.h"
#include "core/predict/chordminimodel.h"
#include "core/project/project.h"
#include "ui/copy/copyrecorder.h"
#include "ui/copy/modelfiles.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"

#include <QUrl>

#include <array>
#include <memory>
#include <optional>

// Chord prediction (copy-spec.md section 4 "코드", ChordMini): copy.predict.chords fetches the model on first
// use (the same inline offer as the beat tracker), runs the CQT and the model as a Prediction job over the
// active recording, and shows the symbols as a grey layer above the score. copy.predict.accept (when that
// layer was the last prediction shown) writes them as chord symbols into the selected measures, or every
// measure, at the quarter each change starts on; the layer then hides what was accepted.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr std::string_view predictKind = "chords";
constexpr double mapRate = 1000.0; // the same millisecond map the audio track uses
constexpr std::uint64_t mebibyte = 1024ULL * 1024ULL;

} // namespace

void Shell::wireChordPredictActions()
{
    connect(m_actions->action(Id::CopyPredictChords), &QAction::triggered, this, &Shell::runChordPrediction);
}

/// With the model present the job starts at once; otherwise the user is asked before anything is fetched.
void Shell::runChordPrediction()
{
    if (!m_project->activeSourcePath()) {
        showStatus(tr("코드 예측은 음원이 있어야 합니다 (파일 › 불러오기…)"));
        return;
    }
    if (m_predicting) {
        showStatus(tr("예측이 이미 진행 중입니다"));
        return;
    }
    const std::array<predict::ModelFile, 1> needed{predict::chordMiniModel};
    const std::vector<predict::ModelFile> missing = missingModels(chordMiniDir(*m_paths), needed);
    if (missing.empty()) {
        startChordPrediction();
        return;
    }
    offerModelDownload({tr("코드 예측 모델이 아직 없습니다 (ChordMini, %1 MB). 공식 가중치에서 변환한 파일을 "
                           "GitHub 릴리스에서 내려받을까요? 앱에는 동봉되지 않습니다")
                            .arg(predict::chordMiniModel.size / mebibyte),
                        QUrl(QString::fromUtf8(predict::chordMiniReleaseUrl.data(),
                                               static_cast<qsizetype>(predict::chordMiniReleaseUrl.size()))),
                        chordMiniDir(*m_paths),
                        missing,
                        [this] {
                            runChordPrediction();
                        }});
}

/// The Prediction job: decode, tuning, CQT, the model, merge; the layer appears when done.
void Shell::startChordPrediction()
{
    const auto path = m_project->activeSourcePath();
    const auto sourceId = m_project->activeSourceId();
    if (!path || !sourceId) {
        return;
    }
    const auto result = std::make_shared<std::vector<predict::ChordSegment>>();
    const jobs::Work work = [audio = *path,
                             model = modelPath(chordMiniDir(*m_paths), predict::chordMiniModel),
                             result](const std::stop_token &stop, const jobs::Report &report) -> Result<void> {
        Result<std::vector<predict::ChordSegment>> segments = predict::predictChords(audio, model, stop, report);
        if (!segments) {
            return std::unexpected(segments.error());
        }
        *result = std::move(*segments);
        return {};
    };
    if (m_jobs == nullptr) { // tests: inline
        const std::stop_source never;
        const Result<void> r = work(never.get_token(), [](double) { });
        if (r) {
            showChordPrediction(*result, *sourceId);
        } else {
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
        if (!event.error) {
            showChordPrediction(*result, sourceId);
        }
    });
}

/// Ticks from the active version's sync points (none: the score's tempo from zero); flats when the score
/// starts in a flat key, sharps otherwise.
model::Prediction Shell::chordPredictionItems() const
{
    const model::Score &score = m_editor->score();
    const model::TimeMap map =
        model::TimeMap::fromSyncPoints(score,
                                       project::syncPointsFor(m_project->project(), m_chordPredictionSource),
                                       mapRate);
    const bool flats =
        !score.measures.empty() && score.measures.front().keySig && score.measures.front().keySig->fifths < 0;
    return predict::toPrediction(m_chordSegments.value_or(std::vector<predict::ChordSegment>{}),
                                 m_chordPredictionSource,
                                 map,
                                 flats);
}

void Shell::showChordPrediction(const std::vector<predict::ChordSegment> &segments,
                                const model::Id &sourceId,
                                bool fromCache)
{
    m_chordSegments = segments;
    m_chordPredictionSource = sourceId;
    m_shownPrediction = ShownPrediction::Chords;
    const model::Prediction prediction = chordPredictionItems();
    m_scoreView->setPredictedChords(prediction.items);
    updatePredictActions();
    if (fromCache) {
        return;
    }
    savePredictionCache(sourceId);
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictShow(predictKind);
    }
    showStatus(tr("코드 예측: %1개 · %2 — 마디를 선택하고 카피 › 예측 받아들이기 (선택 없으면 전체)")
                   .arg(prediction.items.size())
                   .arg(providerLabel()));
}

void Shell::acceptChordPrediction()
{
    const auto sourceId = m_project->activeSourceId();
    if (!m_chordSegments || !sourceId || *sourceId != m_chordPredictionSource || m_editor->score().measures.empty()) {
        showStatus(tr("받아들일 코드 예측이 없습니다 (카피 › 코드 예측 실행…)"));
        return;
    }
    const model::Prediction prediction = chordPredictionItems();
    const auto selected = m_scoreView->selectedMeasures();
    const edit::MeasureSpan span{selected ? selected->first : 0,
                                 selected ? selected->second : m_editor->score().measures.size() - 1};
    if (!m_editor->runCommand(edit::acceptPrediction(m_project->project(), prediction, span, 0, m_rangeIds))) {
        showStatus(tr("마디 %1~%2에 받아들일 코드가 없습니다").arg(span.from + 1).arg(span.to + 1));
        return;
    }
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictAccept(predictKind, prediction.items.size());
    }
    pushCopyScore();
    hideAcceptedChords(span);
    showStatus(tr("예측 코드 받아들임: 마디 %1~%2").arg(span.from + 1).arg(span.to + 1));
}

/// The grey symbols inside the accepted measures would sit on the real ones now: only the others stay.
void Shell::hideAcceptedChords(edit::MeasureSpan span)
{
    std::vector<model::PredictedItem> remaining;
    for (const model::PredictedItem &item : m_scoreView->session().predictedChords) {
        const auto measure = model::measureAtTick(m_editor->score(), item.tick);
        if (!measure || *measure < span.from || *measure > span.to) {
            remaining.push_back(item);
        }
    }
    m_scoreView->setPredictedChords(std::move(remaining));
}

void Shell::clearChordPrediction()
{
    m_chordSegments.reset();
    m_chordPredictionSource.clear();
    m_scoreView->setPredictedChords({});
    if (m_shownPrediction == ShownPrediction::Chords) {
        m_shownPrediction = ShownPrediction::None;
    }
    updatePredictActions();
}

} // namespace cc::ui
