// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/statsmodel.h"

#include "ui/routine/routinetimer.h"
#include "ui/shell/settings.h"

#include <array>
#include <chrono>

namespace cc::ui
{

namespace
{

constexpr int secondsPerMinute = 60;

/// "01".."04" -> 0..3; -1 for any other id. A store written by another version may name a module this
/// build does not have; its seconds are shown nowhere rather than credited to module 01 (stats.md section 9).
int moduleIndexOrNone(const std::string &moduleId)
{
    const bool twoDigits = moduleId.size() == 2 && moduleId[0] == '0';
    const int number = twoDigits ? moduleId[1] - '0' : 0;
    return number >= 1 && number <= StatsModel::moduleCount ? number - 1 : -1;
}

/// Adds a per-module query result to today's array, dropping ids this build has no module for.
void creditToday(const stats::SecondsPerModule &perModule, std::array<int, StatsModel::moduleCount> &today)
{
    for (const auto &[moduleId, seconds] : perModule) {
        const int index = moduleIndexOrNone(moduleId);
        if (index >= 0) {
            today[static_cast<std::size_t>(index)] += seconds;
        }
    }
}

} // namespace

StatsModel::StatsModel(const stats::IStatsReader *reader,
                       const Settings &settings,
                       const RoutineTimerController &timer,
                       QObject *parent)
    : QObject(parent)
    , m_reader(reader)
    , m_settings(&settings)
    , m_timer(&timer)
{
    connect(m_timer, &RoutineTimerController::changed, this, &StatsModel::onTimerChanged);
    connect(m_settings, &Settings::valueChanged, this, &StatsModel::onSettingChanged);
    onTimerChanged();
}

bool StatsModel::hasRecords() const
{
    return !m_perDay.empty() || !m_coverage.empty();
}

int StatsModel::todaySeconds(int module) const
{
    return module >= 0 && module < moduleCount ? m_today[static_cast<std::size_t>(module)] : 0;
}

int StatsModel::todayMinutes() const
{
    int seconds = 0;
    for (const int each : m_today) {
        seconds += each;
    }
    return seconds / secondsPerMinute;
}

int StatsModel::streakDays() const
{
    return m_streak;
}

const stats::KeyCoverage &StatsModel::coverage() const
{
    return m_coverage;
}

void StatsModel::refresh()
{
    m_today.fill(0);
    m_perDay.clear();
    m_coverage.clear();
    m_streak = 0;
    if (m_reader != nullptr) {
        query();
    }
    emit changed();
}

/// A failed query leaves that part empty: the dock then shows less, never an error (the store reported an
/// unreadable file at startup already, stats.md section 7).
void StatsModel::query()
{
    const stats::TimePoint now = std::chrono::system_clock::now();
    const stats::LocalDay today = stats::localDayOf(now, nullptr);
    if (const auto r = m_reader->routineSecondsPerModule(stats::startOfLocalDay(today, nullptr))) {
        creditToday(*r, m_today);
    }
    if (auto r = m_reader->routineSecondsPerDay()) {
        m_perDay = std::move(*r);
    }
    const stats::LocalDay windowStart = today - std::chrono::days{coverageDays - 1}; // 28 days including today
    if (auto r = m_reader->routineKeyCoverage(stats::startOfLocalDay(windowStart, nullptr))) {
        m_coverage = std::move(*r);
    }
    const int minSeconds = m_settings->intValue(setting::Key::StatsMinDayMinutes) * secondsPerMinute;
    m_streak = stats::streakDays(m_perDay, today, minSeconds);
}

/// The recorder listens to the same signal and was connected first, so its event is stored before we query.
void StatsModel::onTimerChanged()
{
    const routine::RoutineTimer &timer = m_timer->timer();
    const int minute = timer.elapsedSeconds(timer.module()) / secondsPerMinute;
    const int state = static_cast<int>(timer.state());
    if (minute != m_shownMinute || state != m_shownState) {
        m_shownMinute = minute;
        m_shownState = state;
        refresh();
    }
}

void StatsModel::onSettingChanged(setting::Key key)
{
    if (key == setting::Key::StatsMinDayMinutes) {
        const int minSeconds = m_settings->intValue(key) * secondsPerMinute;
        m_streak =
            stats::streakDays(m_perDay, stats::localDayOf(std::chrono::system_clock::now(), nullptr), minSeconds);
        emit changed();
    } else if (key == setting::Key::PracticeTimerMinutes) {
        emit changed(); // the bars' targets changed
    }
}

} // namespace cc::ui
