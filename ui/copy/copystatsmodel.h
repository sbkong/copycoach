// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/command.h"
#include "core/stats/reader.h"

#include <QObject>

#include <string>
#include <vector>

// What the stats dock shows in the copy context (stats.md section 3, copy-spec.md section 8): how long this
// song's recording has played today and in all, and one row per loop (saved ranges by name, plus whatever
// was played outside one). Re-queried when the project or its versions change and whenever playback stops,
// so the database is read at most once per play.
namespace cc::ui
{

class AudioService;
class ProjectSession;

class CopyStatsModel : public QObject
{
    Q_OBJECT
public:
    struct LoopRow {
        std::string rangeId; // empty = outside a saved range
        QString name;
        stats::CopyLoopStats stats;
    };

    /// `reader` nullptr (recording off, database unreadable, tests) means no records.
    CopyStatsModel(const stats::IStatsReader *reader,
                   const ProjectSession &project,
                   const AudioService &audio,
                   QObject *parent = nullptr);

    [[nodiscard]] bool hasProject() const;
    [[nodiscard]] bool hasRecords() const; // any loop of this project ever
    [[nodiscard]] int todaySeconds() const;
    [[nodiscard]] int totalSeconds() const;
    [[nodiscard]] const stats::SecondsPerStage &stageSeconds() const; // only stages with time
    /// Saved ranges in project order (every one, played or not), then the outside-a-range row when it has plays.
    [[nodiscard]] const std::vector<LoopRow> &rows() const;
    void refresh();

signals:
    void changed();

private:
    void query();
    void buildRows(const stats::CopyLoopStatsByRange &byRange);

    const stats::IStatsReader *m_reader;
    const ProjectSession *m_project;
    int m_today = 0;
    int m_total = 0;
    bool m_hasRecords = false;
    stats::SecondsPerStage m_stageSeconds;
    std::vector<LoopRow> m_rows;
};

} // namespace cc::ui
