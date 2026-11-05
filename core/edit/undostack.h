// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/edit/command.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace cc::edit
{

/// Linear undo history (edit.undo / edit.redo). Pushing applies the command and drops the redo branch; the
/// stack also knows whether the project differs from its last save, for the loss-confirmation dialog
/// (states.md section 5). Autosave never touches it (copy-spec.md section 16.2 B-1).
class UndoStack
{
public:
    static constexpr std::size_t defaultMaxDepth = 1000; // bounded memory, coding-standards.md section 19

    explicit UndoStack(std::size_t maxDepth = defaultMaxDepth);

    /// Applies the command and records it. A null command is a no-op and returns DirtyRange::none().
    DirtyRange push(project::Project &project, std::unique_ptr<Command> command);
    DirtyRange undo(project::Project &project); // none when there is nothing to undo
    DirtyRange redo(project::Project &project);

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    /// True when the project has changed since markSaved() (or since the stack was created or cleared).
    [[nodiscard]] bool modified() const;
    void markSaved();
    void clear(); // after loading another project

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    std::size_t m_applied = 0; // commands[0, m_applied) are in effect
    std::optional<std::size_t> m_savedAt = 0; // m_applied at the last save; nullopt once that state fell off
    std::size_t m_maxDepth;
};

} // namespace cc::edit
