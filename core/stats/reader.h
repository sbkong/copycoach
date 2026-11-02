// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <chrono>
#include <map>
#include <string>

// The read side of the practice records (stats.md section 5): the dock and the status bar draw the plain
// structs these queries return and know nothing about SQLite. Times are UTC instants; "a day" is a local
// calendar date (stats.md section 9), so every per-day result is keyed by local_days.
namespace cc::stats
{

using TimePoint = std::chrono::system_clock::time_point;
/// A local calendar date (midnight-to-midnight in the zone the store was given), never an instant.
using LocalDay = std::chrono::local_days;
/// Module id ("01".."04") -> seconds.
using SecondsPerModule = std::map<std::string, int>;
using SecondsPerDay = std::map<LocalDay, int>;
/// Module id -> note name ("C", "Db", ...) -> how many module starts listed that key.
using KeyCoverage = std::map<std::string, std::map<std::string, int>>;

class IStatsReader
{
public:
    virtual ~IStatsReader() = default;
    /// Seconds practised per module since `since` (the running totals' largest value per session, summed).
    [[nodiscard]] virtual Result<SecondsPerModule> routineSecondsPerModule(TimePoint since) const = 0;
    /// Seconds practised per local day over the whole history: each increase of a running total counts on the
    /// day it was reported, so a module that runs past midnight is split between the two days.
    [[nodiscard]] virtual Result<SecondsPerDay> routineSecondsPerDay() const = 0;
    /// Keys listed by routine.module.start events since `since`, counted per module (the 12 x 4 heatmap).
    [[nodiscard]] virtual Result<KeyCoverage> routineKeyCoverage(TimePoint since) const = 0;
};

/// The local calendar date of `time`; `zone` nullptr means the system zone.
[[nodiscard]] LocalDay localDayOf(TimePoint time, const std::chrono::time_zone *zone);
/// The instant local midnight starts `day`; `zone` nullptr means the system zone.
[[nodiscard]] TimePoint startOfLocalDay(LocalDay day, const std::chrono::time_zone *zone);
/// Consecutive days ending today, or yesterday when today has nothing yet, with at least `minSeconds`
/// (stats.md section 4 "연속 일수": a day counts from stats.min-day-minutes).
[[nodiscard]] int streakDays(const SecondsPerDay &perDay, LocalDay today, int minSeconds);

} // namespace cc::stats
