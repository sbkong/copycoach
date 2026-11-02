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
    int index = 0; // 1..8
    int degree = 0; // 1..7
    ChordQuality quality = ChordQuality::Maj7; // diatonic seventh quality of that degree, Dom7 for a secondary
    PitchClass rootPitchClass = 0;
    StringFret root{}; // string 6 or 5, fret after the position rule
    int target = 0; // secondary dominant: the degree this V7 resolves to (section 2.5); 0 for a diatonic slot
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

/// Secondary dominant mode (section 2.5): the circle-of-fifths loop where every slot whose root is a fifth above the
/// next root (and is not V itself) becomes the V7 of that next chord. Roots and frets stay as in shellLoop.
std::array<ShellSlot, 8> secondaryDominantLoop(PitchClass key, const Tuning &tuning);

/// Which tensions a secondary dominant takes (section 2.6): Diatonic = the key's own notes in the 9 / 13 places,
/// Chromatic = b9 / b13 whenever the target chord is minor. They differ only on V7/ii.
enum class TensionRule : std::uint8_t {
    Diatonic,
    Chromatic,
};

/// Upper layer of a secondary-dominant slot (section 2.6); diatonic slots fall back to upperNotes. A root on string 5
/// resolving to a minor chord puts the fifth, not the b13, on string 1 (the "easy version").
std::array<UpperNote, 2>
secondaryUpperNotes(const ShellSlot &slot, PitchClass key, TensionRule rule, const Tuning &tuning);

} // namespace cc::theory
