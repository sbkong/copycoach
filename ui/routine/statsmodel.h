// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/routine/timer.h"
#include "core/stats/reader.h"
#include "generated/settingkeys.h"

#include <QObject>

#include <array>

// What the stats dock and the status bar draw in the routine context (stats.md section 3): today's seconds
// per module, the streak and the 28-day key coverage, read through IStatsReader. "Today" starts at local
// midnight (stats.md section 9). The queries run when the timer's shown minute or state changes, not on
// every second, so the database is read at most once a minute while practising.
namespace cc::ui
{

class RoutineTimerController;
class Settings;

class StatsModel : public QObject
{
    Q_OBJECT
public:
    static constexpr int moduleCount = routine::RoutineTimer::moduleCount;
    static constexpr int coverageDays = 28; // stats.md section 3: the heatmap window

    /// `reader` nullptr (recording off, database unreadable, tests) means no records.
    StatsModel(const stats::IStatsReader *reader,
               const Settings &settings,
               const RoutineTimerController &timer,
               QObject *parent = nullptr);

    [[nodiscard]] bool hasRecords() const;
    [[nodiscard]] int todaySeconds(int module) const;
    [[nodiscard]] int todayMinutes() const;
    [[nodiscard]] int streakDays() const;
    [[nodiscard]] const stats::KeyCoverage &coverage() const;
    /// Re-runs the queries now; the timer and the settings call it on their own.
    void refresh();

signals:
    void changed();

private:
    void query();
    void onTimerChanged();
    void onSettingChanged(setting::Key key);

    const stats::IStatsReader *m_reader;
    const Settings *m_settings;
    const RoutineTimerController *m_timer;
    std::array<int, moduleCount> m_today{};
    stats::SecondsPerDay m_perDay;
    stats::KeyCoverage m_coverage;
    int m_streak = 0;
    int m_shownMinute = -1;
    int m_shownState = -1;
};

} // namespace cc::ui
