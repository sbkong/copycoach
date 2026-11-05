// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/snapshot.h"

#include <cassert>
#include <iterator>

namespace cc::edit
{

MeasureEdit::MeasureEdit(std::size_t from, std::vector<model::Measure> after, bool timing)
    : m_from(from)
    , m_after(std::move(after))
    , m_timing(timing)
{
}

void MeasureEdit::apply(project::Project &project)
{
    auto &measures = project.score.measures;
    assert(m_from + m_after.size() <= measures.size());
    m_before.assign(measures.begin() + static_cast<std::ptrdiff_t>(m_from),
                    measures.begin() + static_cast<std::ptrdiff_t>(m_from + m_after.size()));
    for (std::size_t i = 0; i < m_after.size(); ++i) {
        measures[m_from + i] = m_after[i];
    }
}

void MeasureEdit::undo(project::Project &project)
{
    auto &measures = project.score.measures;
    for (std::size_t i = 0; i < m_before.size(); ++i) {
        measures[m_from + i] = m_before[i];
    }
}

DirtyRange MeasureEdit::dirty() const
{
    return DirtyRange::measures(m_from, m_from + m_after.size() - 1, m_timing);
}

ShellEdit::ShellEdit(project::Project shell,
                     std::size_t from,
                     std::size_t removed,
                     std::vector<model::Measure> measures,
                     DirtyRange dirty)
    : m_shell(std::move(shell))
    , m_from(from)
    , m_removed(removed)
    , m_measures(std::move(measures))
    , m_dirty(dirty)
{
}

/// Swaps the shell and splices the measures. The same call undoes it, because the stored shell and span are
/// replaced by what they displaced.
void ShellEdit::apply(project::Project &project)
{
    std::vector<model::Measure> measures = std::move(project.score.measures);
    assert(m_from + m_removed <= measures.size());
    const auto first = measures.begin() + static_cast<std::ptrdiff_t>(m_from);
    const auto last = first + static_cast<std::ptrdiff_t>(m_removed);
    std::vector<model::Measure> displaced(std::make_move_iterator(first), std::make_move_iterator(last));
    measures.erase(first, last);
    measures.insert(measures.begin() + static_cast<std::ptrdiff_t>(m_from),
                    std::make_move_iterator(m_measures.begin()),
                    std::make_move_iterator(m_measures.end()));
    const std::size_t inserted = m_measures.size();
    m_measures = std::move(displaced);
    m_removed = inserted;
    std::swap(project, m_shell);
    project.score.measures = std::move(measures);
    m_shell.score.measures.clear();
}

void ShellEdit::undo(project::Project &project)
{
    apply(project);
}

DirtyRange ShellEdit::dirty() const
{
    return m_dirty;
}

} // namespace cc::edit
