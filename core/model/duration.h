// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/constants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Musical time in integer ticks, 960 per quarter note (coding-standards.md section 17, PLAN 4-2 principle 7).
namespace cc::model
{

using Tick = std::int64_t;

enum class Dur : std::uint8_t {
    Whole,
    Half,
    Quarter,
    Eighth,
    Sixteenth,
    ThirtySecond,
    SixtyFourth,
};

struct Tuplet {
    int num; // notes played ...
    int numbase; // ... in the time of this many
};

struct TimeSignature {
    int count = 4; // beats per measure
    int unit = 4; // beat note value: 4 = quarter, 8 = eighth
    constexpr bool operator==(const TimeSignature &) const = default;
};

/// Ticks of a plain note value: whole = 3840, quarter = 960, eighth = 480 ...
constexpr Tick baseTicks(Dur dur)
{
    constexpr Tick quarter = theory::ticksPerQuarter;
    constexpr std::array<Tick, 7>
        ticks{quarter * 4, quarter * 2, quarter, quarter / 2, quarter / 4, quarter / 8, quarter / 16};
    return ticks[static_cast<std::size_t>(dur)];
}

/// Ticks of a note with dots and an optional tuplet ratio. Dotted: 1.5x, double-dotted: 1.75x.
constexpr Tick durationTicks(Dur dur, int dots = 0, std::optional<Tuplet> tuplet = std::nullopt)
{
    Tick ticks = baseTicks(dur);
    Tick extra = ticks;
    for (int i = 0; i < dots; ++i) {
        extra /= 2;
        ticks += extra;
    }
    if (tuplet) {
        ticks = ticks * tuplet->numbase / tuplet->num;
    }
    return ticks;
}

/// Note value of one beat for a time signature unit (4 -> quarter, 8 -> eighth).
constexpr Dur beatDur(const TimeSignature &timeSig)
{
    switch (timeSig.unit) {
    case 1:
        return Dur::Whole;
    case 2:
        return Dur::Half;
    case 8:
        return Dur::Eighth;
    case 16:
        return Dur::Sixteenth;
    default:
        return Dur::Quarter;
    }
}

constexpr Tick measureTicks(const TimeSignature &timeSig)
{
    return baseTicks(beatDur(timeSig)) * timeSig.count;
}

} // namespace cc::model
