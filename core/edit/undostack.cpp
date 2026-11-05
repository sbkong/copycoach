// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/undostack.h"

namespace cc::edit
{

UndoStack::UndoStack(std::size_t maxDepth)
    : m_maxDepth(maxDepth > 0 ? maxDepth : 1)
{
}

DirtyRange UndoStack::push(project::Project &project, std::unique_ptr<Command> command)
{
    if (!command) {
        return DirtyRange::none();
    }
    m_commands.resize(m_applied); // the redo branch is gone
    if (m_savedAt && *m_savedAt > m_applied) {
        m_savedAt.reset(); // the saved state was on that branch
    }
    command->apply(project);
    const DirtyRange dirty = command->dirty();
    m_commands.push_back(std::move(command));
    if (m_commands.size() > m_maxDepth) {
        m_commands.erase(m_commands.begin());
        if (m_savedAt) {
            m_savedAt = *m_savedAt == 0 ? std::nullopt : std::optional{*m_savedAt - 1};
        }
    }
    m_applied = m_commands.size();
    return dirty;
}

DirtyRange UndoStack::undo(project::Project &project)
{
    if (!canUndo()) {
        return DirtyRange::none();
    }
    --m_applied;
    m_commands[m_applied]->undo(project);
    return m_commands[m_applied]->dirty();
}

DirtyRange UndoStack::redo(project::Project &project)
{
    if (!canRedo()) {
        return DirtyRange::none();
    }
    m_commands[m_applied]->apply(project);
    ++m_applied;
    return m_commands[m_applied - 1]->dirty();
}

bool UndoStack::canUndo() const
{
    return m_applied > 0;
}

bool UndoStack::canRedo() const
{
    return m_applied < m_commands.size();
}

bool UndoStack::modified() const
{
    return !m_savedAt || *m_savedAt != m_applied;
}

void UndoStack::markSaved()
{
    m_savedAt = m_applied;
}

void UndoStack::markUnsaved()
{
    m_savedAt.reset();
}

std::size_t UndoStack::undoDepth() const
{
    return m_applied;
}

void UndoStack::clear()
{
    m_commands.clear();
    m_applied = 0;
    m_savedAt = 0;
}

} // namespace cc::edit
