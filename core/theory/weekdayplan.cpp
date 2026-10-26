// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/weekdayplan.h"

namespace cc::theory
{

namespace
{

constexpr int perfectFourth = 5;

/// Circle of fifths going flat-wards (up a fourth): C F Bb Eb Ab Db Gb B E A D G.
PitchClass circleKey(int index)
{
    return transpose(0, perfectFourth * index);
}

} // namespace

WeekdayPlan defaultWeekdayPlan(Weekday day)
{
    const int dayIndex = static_cast<int>(day); // Sunday 0 ... Saturday 6
    if (day == Weekday::Sunday) {
        return WeekdayPlan{1, {circleKey(0), circleKey(1)}, true};
    }
    const int pairIndex = dayIndex - 1; // Monday 0 ... Saturday 5
    return WeekdayPlan{dayIndex + 1, {circleKey(2 * pairIndex), circleKey((2 * pairIndex) + 1)}, false};
}

} // namespace cc::theory
