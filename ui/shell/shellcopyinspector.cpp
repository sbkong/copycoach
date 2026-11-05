// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "core/project/project.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/copyinspector.h"
#include "ui/copy/copyrecorder.h"
#include "ui/copy/projectsession.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"

#include <algorithm>
#include <cmath>

// Ranges (copy-spec.md section 1 `ranges[]`): copy.section.add saves the score selection as a range at the
// current speed, copy.section.remove drops the one picked in the inspector, copy.section.loop-selection loops
// the selected measures in the player. The inspector edits the memos.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr double percent = 100.0;

} // namespace

void Shell::wireCopyInspector()
{
    connect(m_project, &ProjectSession::projectChanged, this, [this] {
        m_copyInspector->setTitle(m_project->title());
        m_copyInspector->refresh();
    });
    connect(m_scoreView, &ScoreView::selectionChanged, this, [this] {
        m_copyInspector->setSelectedMeasures(m_scoreView->selectedMeasures());
    });
    connect(m_copyInspector, &CopyInspector::rangeChosen, this, &Shell::showRange);
    connect(m_actions->action(Id::CopySectionAdd), &QAction::triggered, this, &Shell::addRangeFromSelection);
    connect(m_actions->action(Id::CopySectionRemove), &QAction::triggered, this, &Shell::removeChosenRange);
    connect(m_actions->action(Id::CopySectionLoopSelection), &QAction::triggered, this, [this] {
        loopSelection();
    });
}

/// The range's bars become the score selection and the loop.
void Shell::showRange(const model::Id &id)
{
    for (const project::Range &range : m_project->project().ranges) {
        if (range.id == id) {
            m_scoreView->setSelectedMeasures(std::pair{range.barFrom, range.barTo});
            loopSelection(id);
            return;
        }
    }
}

void Shell::addRangeFromSelection()
{
    const auto selected = m_scoreView->selectedMeasures();
    if (!selected || !m_project->hasProject()) {
        showStatus(tr("구간으로 저장할 마디를 먼저 선택하세요"));
        return;
    }
    const project::Project &project = m_project->project();
    project::Range range;
    range.id = m_rangeIds.next();
    range.name = tr("구간 %1").arg(project.ranges.size() + 1).toStdString();
    range.barFrom = selected->first;
    range.barTo = selected->second;
    range.speedPct = static_cast<int>(std::lround(m_audio->speed() * percent));
    if (!m_editor->runCommand(edit::setRange(project, range))) {
        showStatus(tr("구간을 저장할 수 없습니다 (마디 %1~%2)").arg(selected->first + 1).arg(selected->second + 1));
        return;
    }
    m_copyInspector->selectRange(range.id);
    showStatus(tr("구간 저장: 마디 %1~%2").arg(selected->first + 1).arg(selected->second + 1));
}

void Shell::removeChosenRange()
{
    const auto id = m_copyInspector->selectedRange();
    if (!id) {
        showStatus(tr("삭제할 구간을 목록에서 고르세요"));
        return;
    }
    m_copyInspector->commitNow();
    if (m_editor->runCommand(edit::removeRange(m_project->project(), *id))) {
        showStatus(tr("구간 삭제"));
    }
}

/// Loop the selected measures (copy mode: the recording wraps; routine mode: nothing to loop here).
void Shell::loopSelection(std::optional<model::Id> rangeId)
{
    const auto selected = m_scoreView->selectedMeasures();
    if (!selected || !m_audio->copyMode()) {
        return;
    }
    const model::Score &score = m_editor->score();
    const std::size_t end = std::min(selected->second + 1, score.measures.size());
    m_audio->setCopyLoop(score.measureStartTick(selected->first), score.measureStartTick(end));
    m_actions->action(Id::TransportLoop)->setChecked(true);
    if (m_copyRecorder != nullptr) {
        m_copyRecorder->setLoop(*selected, std::move(rangeId));
    }
    showStatus(tr("루프: 마디 %1~%2").arg(selected->first + 1).arg(selected->second + 1));
}

} // namespace cc::ui
