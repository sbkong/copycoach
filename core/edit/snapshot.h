// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/edit/command.h"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

// The three command shapes behind commands.h. All of them undo by snapshot: a factory computes the new state
// from the current model, apply stores what it replaces, undo puts it back. No command has edit logic of its
// own, so apply is repeatable and every factory stays a pure function of (model, arguments).
namespace cc::edit
{

/// Replaces one field of the project (a string, a vector, a struct) reached through `field`.
template<typename T>
class ValueEdit final : public Command
{
public:
    using Field = std::function<T &(project::Project &)>;

    ValueEdit(Field field, T value, DirtyRange dirty)
        : m_field(std::move(field))
        , m_new(std::move(value))
        , m_dirty(dirty)
    {
    }

    void apply(project::Project &project) override
    {
        m_old = m_field(project);
        m_field(project) = m_new;
    }

    void undo(project::Project &project) override
    {
        m_field(project) = m_old;
    }

    [[nodiscard]] DirtyRange dirty() const override
    {
        return m_dirty;
    }

private:
    Field m_field;
    T m_new;
    T m_old{};
    DirtyRange m_dirty;
};

/// Replaces the content of measures [from, from + after.size()); the measure count stays.
class MeasureEdit final : public Command
{
public:
    MeasureEdit(std::size_t from, std::vector<model::Measure> after, bool timing);

    void apply(project::Project &project) override;
    void undo(project::Project &project) override;
    [[nodiscard]] DirtyRange dirty() const override;

private:
    std::size_t m_from;
    std::vector<model::Measure> m_after;
    std::vector<model::Measure> m_before;
    bool m_timing;
};

/// Everything but the measure list, for edits that move references around (measure insert/delete, range
/// removal): the measures are spliced, the rest of the project is swapped whole.
class ShellEdit final : public Command
{
public:
    /// `shell` is the new project with its measures emptied; `measures` replace [from, from + removed).
    ShellEdit(project::Project shell,
              std::size_t from,
              std::size_t removed,
              std::vector<model::Measure> measures,
              DirtyRange dirty);

    void apply(project::Project &project) override;
    void undo(project::Project &project) override;
    [[nodiscard]] DirtyRange dirty() const override;

private:
    project::Project m_shell;
    std::size_t m_from;
    std::size_t m_removed;
    std::vector<model::Measure> m_measures;
    DirtyRange m_dirty;
};

} // namespace cc::edit
