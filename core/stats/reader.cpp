// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/stats/reader.h"

namespace cc::stats
{

namespace
{

const std::chrono::time_zone *zoneOr(const std::chrono::time_zone *zone)
{
    return zone != nullptr ? zone : std::chrono::current_zone();
}

} // namespace

LocalDay localDayOf(TimePoint time, const std::chrono::time_zone *zone)
{
    return std::chrono::floor<std::chrono::days>(zoneOr(zone)->to_local(time));
}

TimePoint startOfLocalDay(LocalDay day, const std::chrono::time_zone *zone)
{
    // A DST gap at midnight is impossible in the zones the app runs in; `earliest` keeps it total anyway.
    return zoneOr(zone)->to_sys(std::chrono::local_seconds(day), std::chrono::choose::earliest);
}

int streakDays(const SecondsPerDay &perDay, LocalDay today, int minSeconds)
{
    const auto counts = [&perDay, minSeconds](LocalDay day) {
        const auto it = perDay.find(day);
        return it != perDay.end() && it->second >= minSeconds;
    };
    LocalDay day = today;
    if (!counts(day)) {
        day -= std::chrono::days{1}; // today has nothing yet: the streak is still alive from yesterday
    }
    int streak = 0;
    while (counts(day)) {
        ++streak;
        day -= std::chrono::days{1};
    }
    return streak;
}

} // namespace cc::stats
