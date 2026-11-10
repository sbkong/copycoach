// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <chrono>
#include <filesystem>
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

/// What the copy workspace did with one loop (copy-spec.md section 8 copy.loop.stop): a saved range by its
/// id, or everything played outside a range under the empty key.
struct CopyLoopStats {
    int plays = 0;
    int seconds = 0;
    int repeats = 0; // loop wraps summed over the plays
    int maxSpeedPct = 0; // the fastest the loop was ever played
    int lastSpeedPct = 0; // of the most recent play
};
using CopyLoopStatsByRange = std::map<std::string, CopyLoopStats>;
/// Stage 1..7 -> seconds the project was open in that stage (copy-spec.md section 8).
using SecondsPerStage = std::map<int, int>;

class IStatsReader
{
public:
    virtual ~IStatsReader() = default;
    /// Seconds practised per module since `since`: the growth of each running total reported in the window.
    [[nodiscard]] virtual Result<SecondsPerModule> routineSecondsPerModule(TimePoint since) const = 0;
    /// Seconds practised per local day over the whole history: each increase of a running total counts on the
    /// day it was reported, so a module that runs past midnight is split between the two days.
    [[nodiscard]] virtual Result<SecondsPerDay> routineSecondsPerDay() const = 0;
    /// Keys listed by routine.module.start events since `since`, counted per module (the 12 x 4 heatmap).
    [[nodiscard]] virtual Result<KeyCoverage> routineKeyCoverage(TimePoint since) const = 0;
    /// Seconds the recording of `projectId` played in copy mode since `since` (copy.loop.stop durations).
    [[nodiscard]] virtual Result<int> copyProjectSeconds(const std::string &projectId, TimePoint since) const = 0;
    /// Every loop of the project over the whole history, keyed by range id ("" = outside a saved range).
    [[nodiscard]] virtual Result<CopyLoopStatsByRange> copyLoopStats(const std::string &projectId) const = 0;
    /// Wall time per stage: from each copy.project.open / copy.stage.enter of the project until the next such
    /// event in the session (another project's open ends it) or the session's end (now while it runs).
    [[nodiscard]] virtual Result<SecondsPerStage> copyStageSeconds(const std::string &projectId) const = 0;
    /// Every event as CSV (stats.md section 7: id, session, kind, project, at, type, payload); Unsupported by
    /// a reader that has no file.
    [[nodiscard]] virtual Result<void> exportCsv(const std::filesystem::path &path) const
    {
        return fail(ErrorCode::Unsupported, path.string(), "no export for this reader");
    }
};

/// The local calendar date of `time`; `zone` nullptr means the system zone.
[[nodiscard]] LocalDay localDayOf(TimePoint time, const std::chrono::time_zone *zone);
/// The instant local midnight starts `day`; `zone` nullptr means the system zone.
[[nodiscard]] TimePoint startOfLocalDay(LocalDay day, const std::chrono::time_zone *zone);
/// Consecutive days ending today, or yesterday when today has nothing yet, with at least `minSeconds`
/// (stats.md section 4 "연속 일수": a day counts from stats.min-day-minutes).
[[nodiscard]] int streakDays(const SecondsPerDay &perDay, LocalDay today, int minSeconds);

} // namespace cc::stats
