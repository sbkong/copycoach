// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/tuning.h"

#include <cstdint>
#include <vector>

// Routine module 01 "diagonal major scale": one form that covers the fretboard diagonally, built from a 3-note group
// (7-1-2) and a 4-note group (3-4-5-6) alternating per string. Rules and expected values: docs/spec/routine-spec.md
// section 3.2. Modes are the same pitch set seen from another tonic (section 3.2 rule 1).
namespace cc::theory
{

enum class RootString : std::uint8_t {
    Five = 5,
    Six = 6,
};

enum class ScaleGroup : std::uint8_t {
    Root, // degree 1 of the parent major (drawn with the root token)
    SevenOneTwo, // 3-note group
    ThreeToSix, // 4-note group
};

struct ScaleNote {
    StringFret position;
    int parentDegree; // 1..7 of the parent major scale
    ScaleGroup group;
};

/// Mode numbers 1 = Ionian ... 7 = Locrian. parentMajorOf(Gb, 7) = G.
constexpr int modeCount = 7;
PitchClass parentMajorOf(PitchClass tonic, int mode);

/// Degree label relative to the mode's tonic (section 3.3: labels follow the practised note): Locrian Gb -> Gb = 1.
int modeDegreeOf(int parentDegree, int mode);

/// The full diagonal form of `parentMajor`, ascending in pitch from string 6 to string 1 (21 notes on six strings).
std::vector<ScaleNote> diagonalMajorForm(PitchClass parentMajor, RootString rootOn, const Tuning &tuning);

} // namespace cc::theory
