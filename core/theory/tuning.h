// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/pitch.h"

#include <vector>

namespace cc::theory
{

/// A fretted note position. string 1 = highest string, fret 0 = open (coding-standards.md section 17).
struct StringFret {
    int string;
    int fret;
    constexpr bool operator==(const StringFret &) const = default;
};

/// Open-string pitches and capo of one track (tuning.md section 4). strings[0] is string 1 (highest).
struct Tuning {
    std::vector<Midi> strings;
    int capo = 0;

    static Tuning standardSix(); // E4 B3 G3 D3 A2 E2

    [[nodiscard]] int stringCount() const;
    [[nodiscard]] Midi openPitch(int string) const;
    [[nodiscard]] Midi pitchAt(StringFret position) const; // open + capo + fret
    [[nodiscard]] PitchClass pitchClassAt(StringFret position) const;

    /// Fret on `string` sounding `pitchClass` that lies closest to `nearFret`; ties go to the lower fret.
    /// Candidates are the frets 0..maxFret with that pitch class (one per octave), so maxFret must be at least 11.
    [[nodiscard]] int nearestFret(int string, PitchClass pitchClass, int nearFret, int maxFret = 24) const;
};

} // namespace cc::theory
