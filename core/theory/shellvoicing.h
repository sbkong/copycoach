// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/chord.h"
#include "core/theory/tuning.h"

#include <array>
#include <cstdint>
#include <vector>

// Routine module 02 "shell + drop 2": diatonic loop of 8 slots with shell voicings (R-7-3) and the upper-structure
// layer. Rules and expected values: docs/spec/routine-spec.md sections 2.2-2.4.
namespace cc::theory
{

enum class LoopOrder : std::uint8_t {
    Circle5, // I IV vii iii vi ii V I
    Scale, // I ii iii IV V vi vii I
};

struct ShellSlot {
    int index; // 1..8
    int degree; // 1..7
    ChordQuality quality; // diatonic seventh quality of that degree
    PitchClass rootPitchClass;
    StringFret root; // string 6 or 5, fret after the position rule
};

/// Degree sequence for an order (8 entries, last is always I).
std::array<int, 8> loopDegrees(LoopOrder order);

/// The 8 slots of the loop in `key`, roots alternating between strings 6 and 5, staying in one position
/// (section 2.2 rules 3-4: nearest fret to the previous root; start string chosen so the whole loop fits frets 1..12).
std::array<ShellSlot, 8> shellLoop(PitchClass key, LoopOrder order, const Tuning &tuning);

/// Shell = root, seventh, third. Root on string 6: 7th on string 4, 3rd on string 3. Root on string 5: 3rd on 4, 7th
/// on 3.
std::vector<StringFret> shellNotes(const ShellSlot &slot, const Tuning &tuning);

/// Upper structure: two notes on strings 2 and 1 (section 2.4). Returned with their chord tones for labelling.
struct UpperNote {
    StringFret position;
    ChordTone tone;
};
std::array<UpperNote, 2> upperNotes(const ShellSlot &slot, const Tuning &tuning);

} // namespace cc::theory
