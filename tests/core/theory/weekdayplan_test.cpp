// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 6: weekday plan decisions of 2026-10-25/26.
#include "core/theory/weekdayplan.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace cc::theory;

TEST_CASE("weekday: Monday to Saturday cover all 12 keys in circle-of-fifths pairs, Sunday reviews")
{
    std::set<PitchClass> covered;
    const std::array<Weekday, 6> workDays{Weekday::Monday,
                                          Weekday::Tuesday,
                                          Weekday::Wednesday,
                                          Weekday::Thursday,
                                          Weekday::Friday,
                                          Weekday::Saturday};
    for (const Weekday day : workDays) {
        const WeekdayPlan plan = defaultWeekdayPlan(day);
        REQUIRE_FALSE(plan.review);
        REQUIRE(intervalUp(plan.keys[0], plan.keys[1]) == 5); // the pair is a fourth apart
        covered.insert(plan.keys[0]);
        covered.insert(plan.keys[1]);
    }
    REQUIRE(covered.size() == 12);
    REQUIRE(defaultWeekdayPlan(Weekday::Monday).keys == std::array<PitchClass, 2>{0, 5}); // C F
    REQUIRE(defaultWeekdayPlan(Weekday::Saturday).keys == std::array<PitchClass, 2>{2, 7}); // D G
    const WeekdayPlan sunday = defaultWeekdayPlan(Weekday::Sunday);
    REQUIRE(sunday.review);
    REQUIRE(sunday.keys == std::array<PitchClass, 2>{0, 5});
}

TEST_CASE("weekday: modes run Ionian on Sunday to Locrian on Saturday")
{
    REQUIRE(defaultWeekdayPlan(Weekday::Sunday).mode == 1);
    REQUIRE(defaultWeekdayPlan(Weekday::Monday).mode == 2);
    REQUIRE(defaultWeekdayPlan(Weekday::Saturday).mode == 7);
}
