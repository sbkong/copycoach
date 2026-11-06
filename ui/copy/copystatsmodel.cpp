// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/copystatsmodel.h"

#include "core/project/project.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/projectsession.h"

#include <chrono>

namespace cc::ui
{

CopyStatsModel::CopyStatsModel(const stats::IStatsReader *reader,
                               const ProjectSession &project,
                               const AudioService &audio,
                               QObject *parent)
    : QObject(parent)
    , m_reader(reader)
    , m_project(&project)
{
    connect(m_project, &ProjectSession::projectChanged, this, &CopyStatsModel::refresh);
    connect(m_project, &ProjectSession::sourcesChanged, this, &CopyStatsModel::refresh);
    connect(m_project, &ProjectSession::modifiedChanged, this, &CopyStatsModel::refresh); // ranges renamed or added
    connect(&audio, &AudioService::stateChanged, this, [this](audio::EngineState state) {
        if (state == audio::EngineState::Stopped) {
            refresh(); // the recorder wrote copy.loop.stop on the same signal
        }
    });
    refresh();
}

bool CopyStatsModel::hasProject() const
{
    return m_project->hasProject();
}

bool CopyStatsModel::hasRecords() const
{
    return m_hasRecords;
}

int CopyStatsModel::todaySeconds() const
{
    return m_today;
}

int CopyStatsModel::totalSeconds() const
{
    return m_total;
}

const std::vector<CopyStatsModel::LoopRow> &CopyStatsModel::rows() const
{
    return m_rows;
}

void CopyStatsModel::refresh()
{
    query();
    emit changed();
}

/// "Today" starts at local midnight in the system zone (stats.md section 9).
void CopyStatsModel::query()
{
    m_today = 0;
    m_total = 0;
    m_hasRecords = false;
    m_rows.clear();
    if (m_reader == nullptr || !m_project->hasProject()) {
        return;
    }
    const std::string &id = m_project->project().projectId;
    const auto now = std::chrono::system_clock::now();
    const stats::TimePoint midnight = stats::startOfLocalDay(stats::localDayOf(now, nullptr), nullptr);
    if (const Result<int> today = m_reader->copyProjectSeconds(id, midnight)) {
        m_today = *today;
    }
    const Result<stats::CopyLoopStatsByRange> loops = m_reader->copyLoopStats(id);
    if (!loops) {
        return;
    }
    for (const auto &[range, stat] : *loops) {
        m_total += stat.seconds;
        m_hasRecords = m_hasRecords || stat.plays > 0;
    }
    buildRows(*loops);
}

void CopyStatsModel::buildRows(const stats::CopyLoopStatsByRange &byRange)
{
    for (const project::Range &range : m_project->project().ranges) {
        const auto it = byRange.find(range.id);
        m_rows.push_back(
            {range.id, QString::fromStdString(range.name), it != byRange.end() ? it->second : stats::CopyLoopStats{}});
    }
    const auto outside = byRange.find(std::string());
    if (outside != byRange.end() && outside->second.plays > 0) {
        m_rows.push_back({std::string(), tr("구간 밖 재생"), outside->second});
    }
}

} // namespace cc::ui
