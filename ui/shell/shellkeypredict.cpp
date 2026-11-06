// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/predict/keycents.h"
#include "core/project/project.h"
#include "ui/copy/copyrecorder.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"

#include <memory>
#include <optional>

// Key and tuning reference prediction (copy-spec.md section 4 "키 · 센트", stage 1): copy.predict.key runs
// the own DSP as a Prediction job and offers the result inline in the copy workspace; [받아들이기] writes the
// key signature of the first measure and the project's reference cents as two undoable edits.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr std::string_view predictKind = "key";

} // namespace

void Shell::wireKeyPredictActions()
{
    connect(m_actions->action(Id::CopyPredictKey), &QAction::triggered, this, &Shell::runKeyPrediction);
}

void Shell::runKeyPrediction()
{
    const auto path = m_project->activeSourcePath();
    const auto sourceId = m_project->activeSourceId();
    if (!path || !sourceId) {
        showStatus(tr("키·센트 예측은 음원이 있어야 합니다 (파일 › 불러오기…)"));
        return;
    }
    const auto result = std::make_shared<std::optional<predict::KeyCentsPrediction>>();
    const jobs::Work work = [audio = *path, result](const std::stop_token &stop,
                                                    const jobs::Report &report) -> Result<void> {
        Result<predict::KeyCentsPrediction> prediction = predict::predictKeyCents(audio, stop, report);
        if (!prediction) {
            return std::unexpected(prediction.error());
        }
        *result = *prediction;
        return {};
    };
    if (m_jobs == nullptr) { // tests: inline
        const std::stop_source never;
        const Result<void> r = work(never.get_token(), [](double) { });
        if (r && *result) {
            showKeyPrediction(**result);
        } else if (!r) {
            showStatus(errorText(r.error()));
        }
        return;
    }
    const jobs::JobId id = m_jobs->enqueue(jobs::JobKind::Prediction, work);
    connect(m_jobs, &JobService::finished, this, [this, id, result](const jobs::JobEvent &event) {
        if (event.id == id && !event.error && *result) {
            showKeyPrediction(**result);
        }
    });
}

/// "예측: F major (상관 0.82) · 기준 A = 440 −12 c" with accept or dismiss (states.md section 1: one sentence,
/// cause then action).
void Shell::showKeyPrediction(const predict::KeyCentsPrediction &prediction)
{
    m_keyPrediction = prediction;
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictShow(predictKind);
    }
    const QString text = tr("예측: %1 (상관 %2) · 기준 음정 A = 440 Hz %3%4 c")
                             .arg(QString::fromStdString(predict::keyName(prediction)))
                             .arg(prediction.keyConfidence, 0, 'f', 2)
                             .arg(prediction.cents >= 0.0 ? QStringLiteral("+") : QString())
                             .arg(prediction.cents, 0, 'f', 0);
    m_copyMessage->show(MessageKind::Info,
                        text,
                        {MessageAction{tr("받아들이기"),
                                       [this] {
                                           acceptKeyPrediction();
                                           m_copyMessage->clear();
                                       }},
                         MessageAction{tr("나중에"), [this] {
                                           m_copyMessage->clear();
                                       }}});
}

/// Two edits: the key signature on the first measure (when the score has one) and the reference cents.
void Shell::acceptKeyPrediction()
{
    if (!m_keyPrediction || !m_project->hasProject()) {
        return;
    }
    const predict::KeyCentsPrediction prediction = *m_keyPrediction;
    std::size_t applied = 0;
    if (!m_editor->score().measures.empty()
        && m_editor->runCommand(edit::setKeySig(m_editor->score(), 0, predict::keySignatureOf(prediction)))) {
        ++applied;
    }
    project::Reference reference = m_project->project().reference;
    reference.pitchCents = std::round(prediction.cents);
    if (m_editor->runCommand(edit::setReference(m_project->project(), reference))) {
        ++applied;
    }
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->recordPredictAccept(predictKind, applied);
    }
    m_scoreView->updateScore(m_editor->score());
    showStatus(tr("키 %1 · 기준 음정 %2 c 적용 (편집 %3개)")
                   .arg(QString::fromStdString(predict::keyName(prediction)))
                   .arg(reference.pitchCents, 0, 'f', 0)
                   .arg(applied));
}

} // namespace cc::ui
