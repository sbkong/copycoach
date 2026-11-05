// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/project/project.h"

#include <cstddef>
#include <limits>

// Every change to a project goes through a command (score-data-path.md section 2, copy-spec.md section 16.2 B-1):
// apply and undo touch the model only, and report which measures the views must re-engrave. The cursor and the
// selection are session state, not model state, so a command never carries them. Commands are built by the
// factories in commands.h against the current model and run by an UndoStack; apply must stay repeatable
// (redo calls it again on the same state).
namespace cc::edit
{

/// Measures to re-engrave after a command, inclusive; `timing` means tempo, meter or sync points changed and
/// the TimeMap must be rebuilt as well.
struct DirtyRange {
    std::size_t from = 1;
    std::size_t to = 0;
    bool timing = false;

    static constexpr DirtyRange none()
    {
        return {};
    }
    static constexpr DirtyRange measures(std::size_t from, std::size_t to, bool timing = false)
    {
        return {from, to, timing};
    }
    /// Nothing to re-engrave, but the TimeMap must be rebuilt (sync points).
    static constexpr DirtyRange timingOnly()
    {
        return {1, 0, true};
    }
    static constexpr DirtyRange all(bool timing = false)
    {
        return {0, std::numeric_limits<std::size_t>::max(), timing};
    }
    [[nodiscard]] constexpr bool empty() const
    {
        return from > to;
    }
    [[nodiscard]] constexpr DirtyRange merged(const DirtyRange &other) const
    {
        if (empty()) {
            return {other.from, other.to, timing || other.timing};
        }
        if (other.empty()) {
            return {from, to, timing || other.timing};
        }
        return {from < other.from ? from : other.from, to > other.to ? to : other.to, timing || other.timing};
    }
    constexpr bool operator==(const DirtyRange &) const = default;
};

class Command
{
public:
    virtual ~Command() = default;
    Command(const Command &) = delete;
    Command &operator=(const Command &) = delete;
    Command(Command &&) = delete;
    Command &operator=(Command &&) = delete;

    virtual void apply(project::Project &project) = 0;
    virtual void undo(project::Project &project) = 0;
    [[nodiscard]] virtual DirtyRange dirty() const = 0;

protected:
    Command() = default;
};

} // namespace cc::edit
