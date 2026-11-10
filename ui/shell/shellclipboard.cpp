// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/edit/commands.h"
#include "ui/copy/projectsession.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"

// The measure clipboard of the copy score (ia.md section 2 "편집", GP8 keys): copy takes the selected
// measures (or the cursor's), cut clears them as well, paste writes them from the selection's first measure
// (or the cursor's) on. In memory only: the clipboard is measures, not text, and lives as long as the window.
namespace cc::ui
{

using action::Id;

void Shell::wireClipboardActions()
{
    connect(m_actions->action(Id::EditCopy), &QAction::triggered, this, [this] {
        copyMeasures(false);
    });
    connect(m_actions->action(Id::EditCut), &QAction::triggered, this, [this] {
        copyMeasures(true);
    });
    connect(m_actions->action(Id::EditPaste), &QAction::triggered, this, &Shell::pasteMeasures);
}

/// The selection, else the cursor's measure, else nothing.
std::optional<edit::MeasureSpan> Shell::clipboardSpan() const
{
    if (m_active != workspace::Id::Copy || !m_scoreView->hasScore()) {
        return std::nullopt;
    }
    if (const auto selected = m_scoreView->selectedMeasures()) {
        return edit::MeasureSpan{selected->first, selected->second};
    }
    if (const auto cursor = m_editor->cursor()) {
        return edit::MeasureSpan{cursor->measure, cursor->measure};
    }
    return std::nullopt;
}

void Shell::copyMeasures(bool cut)
{
    const auto span = clipboardSpan();
    if (!span) {
        showStatus(tr("복사할 마디를 선택하세요"));
        return;
    }
    const model::Score &score = m_editor->score();
    m_measureClipboard.assign(score.measures.begin() + static_cast<std::ptrdiff_t>(span->from),
                              score.measures.begin() + static_cast<std::ptrdiff_t>(span->to + 1));
    if (cut && m_editor->runCommand(edit::clearMeasures(m_project->project(), *span))) {
        pushCopyScore();
    }
    showStatus((cut ? tr("마디 %1~%2 잘라냄") : tr("마디 %1~%2 복사")).arg(span->from + 1).arg(span->to + 1));
}

void Shell::pasteMeasures()
{
    const auto span = clipboardSpan();
    if (!span || m_measureClipboard.empty()) {
        showStatus(m_measureClipboard.empty() ? tr("붙여넣을 마디가 없습니다 (먼저 복사)")
                                              : tr("붙여넣을 자리를 선택하세요"));
        return;
    }
    if (!m_editor->runCommand(edit::pasteMeasures(m_project->project(), span->from, m_measureClipboard, m_rangeIds))) {
        return;
    }
    pushCopyScore();
    const std::size_t last = std::min(span->from + m_measureClipboard.size(), m_editor->score().measures.size());
    m_scoreView->setSelectedMeasures(std::pair{span->from, last - 1});
    showStatus(tr("마디 %1~%2에 붙여넣음").arg(span->from + 1).arg(last));
}

} // namespace cc::ui
