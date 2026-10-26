// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/pitch.h"

#include <array>
#include <cstdint>

// Built-in weekday routine plan (docs/spec/routine-spec.md section 6). routines/default.json can override it; this is
// the fallback when that file is missing or broken (routine.md section 7).
namespace cc::theory
{

enum class Weekday : std::uint8_t {
    Sunday,
    Monday,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
};

struct WeekdayPlan {
    int mode; // 1 Ionian ... 7 Locrian (Sunday = 1 ... Saturday = 7)
    std::array<PitchClass, 2> keys; // two keys for the day
    bool review; // Sunday: keys are a default; the user picks (section 6)
};

/// Monday to Saturday walk the circle of fifths two keys a day (C F, Bb Eb, Ab Db, Gb B, E A, D G); Sunday reviews.
WeekdayPlan defaultWeekdayPlan(Weekday day);

} // namespace cc::theory
