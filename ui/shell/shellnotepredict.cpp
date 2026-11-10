// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/model/timemap.h"
#include "core/predict/basicpitch.h"
#include "core/project/project.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/copybottomdock.h"
#include "ui/copy/copyrecorder.h"
#include "ui/copy/noteroll.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"

#include <memory>
#include <optional>

// Note prediction (copy-spec.md section 4 "음", Basic Pitch): copy.predict.notes runs the bundled model as a
// Prediction job over the active recording; the notes show as a grey piano roll in the bottom dock.
// copy.predict.accept (when the roll was the last prediction shown) writes them into track 1 of the selected
// measures, or every measure, through the time map of the active version.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr std::string_view predictKind = "notes";
constexpr double mapRate = 1000.0; // the same millisecond map the audio track uses

} // namespace

void Shell::wireNotePredictActions()
{
    connect(m_actions->action(Id::CopyPredictNotes), &QAction::triggered, this, &Shell::runNotePrediction);
    connect(m_audio, &AudioService::sourceFrameChanged, this, [this](std::int64_t frame) {
        const bool show = m_audio->isPlaying() && m_audio->sampleRate() > 0.0;
        m_copyDock->noteRoll()->setPlayheadSeconds(
            show ? std::optional(static_cast<double>(frame) / m_audio->sampleRate()) : std::nullopt);
    });
}

void Shell::runNotePrediction()
{
    const auto path = m_project->activeSourcePath();
    const auto sourceId = m_project->activeSourceId();
    if (!path || !sourceId) {
        showStatus(tr("음 예측은 음원이 있어야 합니다 (파일 › 불러오기…)"));
        return;
    }
    const auto result = std::make_shared<std::vector<predict::NoteEvent>>();
    const jobs::Work work = [audio = *path,
                             model = m_paths->basicPitchModel(),
                             result](const std::stop_token &stop, const jobs::Report &report) -> Result<void> {
        Result<std::vector<predict::NoteEvent>> notes =
            predict::predictNotes(audio, model, predict::BasicPitchOptions{}, stop, report);
        if (!notes) {
            return std::unexpected(notes.error());
        }
        *result = std::move(*notes);
        return {};
    };
    if (m_jobs == nullptr) { // tests: inline
        const std::stop_source never;
        const Result<void> r = work(never.get_token(), [](double) { });
        if (r) {
            showNotePrediction(*result, *sourceId);
        } else {
            showStatus(errorText(r.error()));
        }
        return;
    }
    const jobs::JobId id = m_jobs->enqueue(jobs::JobKind::Prediction, work);
    connect(m_jobs, &JobService::finished, this, [this, id, result, sourceId = *sourceId](const jobs::JobEvent &event) {
        if (event.id == id && !event.error) {
            showNotePrediction(*result, sourceId);
        }
    });
}

void Shell::showNotePrediction(const std::vector<predict::NoteEvent> &notes, const model::Id &sourceId)
{
    m_noteEvents = notes;
    m_notePredictionSource = sourceId;
    m_shownPrediction = ShownPrediction::Notes;
    const double seconds =
        m_audio->sampleRate() > 0.0 ? static_cast<double>(m_audio->sourceLengthFrames()) / m_audio->sampleRate() : 0.0;
    m_copyDock->noteRoll()->setNotes(notes, seconds);
    m_copyDock->showNoteRoll();
    updatePredictActions();
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictShow(predictKind);
    }
    showStatus(tr("음 예측: %1개 · %2 — 마디를 선택하고 카피 › 예측 받아들이기 (선택 없으면 전체)")
                   .arg(notes.size())
                   .arg(providerLabel()));
}

/// Ticks come from the active version's sync points (no sync point: the score's tempo from zero), positions
/// from track 1's tuning; the accept command drops notes the tuning cannot play.
void Shell::acceptNotePrediction()
{
    const auto sourceId = m_project->activeSourceId();
    if (!m_noteEvents || !sourceId || *sourceId != m_notePredictionSource || m_editor->score().measures.empty()) {
        showStatus(tr("받아들일 음 예측이 없습니다 (카피 › 음 예측 실행…)"));
        return;
    }
    const model::Score &score = m_editor->score();
    const model::TimeMap map =
        model::TimeMap::fromSyncPoints(score, project::syncPointsFor(m_project->project(), *sourceId), mapRate);
    const model::Prediction prediction =
        predict::toPrediction(*m_noteEvents, *sourceId, map, score.tracks.front().tuning);
    const auto selected = m_scoreView->selectedMeasures();
    const edit::MeasureSpan span{selected ? selected->first : 0,
                                 selected ? selected->second : score.measures.size() - 1};
    if (!m_editor->runCommand(edit::acceptPrediction(m_project->project(), prediction, span, 0, m_rangeIds))) {
        showStatus(tr("마디 %1~%2에 받아들일 음이 없습니다").arg(span.from + 1).arg(span.to + 1));
        return;
    }
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictAccept(predictKind, prediction.items.size());
    }
    pushCopyScore();
    showStatus(tr("예측 음 받아들임: 마디 %1~%2").arg(span.from + 1).arg(span.to + 1));
}

void Shell::clearNotePrediction()
{
    m_noteEvents.reset();
    m_notePredictionSource.clear();
    m_copyDock->noteRoll()->clear();
    if (m_shownPrediction == ShownPrediction::Notes) {
        m_shownPrediction = ShownPrediction::None;
    }
    updatePredictActions();
}

} // namespace cc::ui
