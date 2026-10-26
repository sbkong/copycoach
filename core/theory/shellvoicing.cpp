// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/shellvoicing.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace cc::theory
{

namespace
{

constexpr int lowestPlayableFret = 1;
constexpr int highestLoopFret = 12;
// A loop that stays in one position spans 5 frets when it starts on string 6 (8 8 7 7 5 5 3 3 in C) and 7 when it
// starts on string 5 (the tritone step IV -> vii lands one fret off). Anything wider means a position jump.
constexpr int maxLoopSpan = 7;
// The whole voicing (shell + upper layer) of every slot must sit within handReach frets of its root.
constexpr int handReach = 4;
constexpr int searchLimit = highestLoopFret + semitonesPerOctave;

int rootFretAnchor(const Tuning &tuning, int string, PitchClass root, int anchor)
{
    if (anchor >= 0) {
        return anchor;
    }
    const int lowest = intervalUp(pitchClassOf(tuning.openPitch(string)), root); // first slot: lowest fretted root
    return lowest == 0 ? semitonesPerOctave : lowest;
}

int chooseScaleString(const Tuning &tuning, PitchClass root, int anchor)
{
    // Scale order: whichever of strings 6/5 keeps the root closest to the position (tie -> 6).
    const int onSix = tuning.nearestFret(6, root, anchor, searchLimit);
    const int onFive = tuning.nearestFret(5, root, anchor, searchLimit);
    return std::abs(onSix - anchor) <= std::abs(onFive - anchor) ? 6 : 5;
}

std::array<ShellSlot, 8> loopFrom(PitchClass key, LoopOrder order, int startString, const Tuning &tuning)
{
    const auto degrees = loopDegrees(order);
    std::array<ShellSlot, 8> slots{};
    int previousFret = -1;
    int firstFret = -1;
    int previousString = startString == 6 ? 5 : 6; // so the first slot lands on startString
    for (std::size_t i = 0; i < degrees.size(); ++i) {
        const int degree = degrees[i];
        const PitchClass root = degreePitchClass(key, degree);
        // Circle order: strings alternate and each root sits nearest the previous one (6 -> 5 same fret, 5 -> 6 two
        // down). Scale order: the position is the first root's fret; each root takes the string that stays closest to
        // it.
        int string = previousString == 6 ? 5 : 6;
        int anchor = previousFret;
        if (order == LoopOrder::Scale && firstFret >= 0) {
            anchor = firstFret;
            string = chooseScaleString(tuning, root, anchor);
        }
        anchor = rootFretAnchor(tuning, string, root, anchor);
        const int fret = tuning.nearestFret(string, root, anchor, searchLimit);
        slots[i] = ShellSlot{static_cast<int>(i) + 1, degree, diatonicSeventh(degree), root, StringFret{string, fret}};
        previousFret = fret;
        previousString = string;
        firstFret = firstFret < 0 ? fret : firstFret;
    }
    return slots;
}

StringFret toneOn(int string, const ShellSlot &slot, ChordTone tone, const Tuning &tuning)
{
    const PitchClass pitchClass = transpose(slot.rootPitchClass, chordToneInterval(slot.quality, tone));
    return StringFret{string, tuning.nearestFret(string, pitchClass, slot.root.fret)};
}

bool voicingInReach(const ShellSlot &slot, const Tuning &tuning)
{
    const auto inReach = [&](const StringFret &note) {
        return std::abs(note.fret - slot.root.fret) <= handReach;
    };
    const auto upper = upperNotes(slot, tuning);
    return std::ranges::all_of(shellNotes(slot, tuning), inReach)
        && std::ranges::all_of(upper, [&](const UpperNote &u) {
               return inReach(u.position);
           });
}

bool fitsPosition(const std::array<ShellSlot, 8> &slots, const Tuning &tuning)
{
    const auto [lowest, highest] = std::ranges::minmax(slots, {}, [](const ShellSlot &s) {
        return s.root.fret;
    });
    const bool rootsFit = lowest.root.fret >= lowestPlayableFret && highest.root.fret <= highestLoopFret
        && highest.root.fret - lowest.root.fret <= maxLoopSpan;
    return rootsFit && std::ranges::all_of(slots, [&](const ShellSlot &s) {
               return voicingInReach(s, tuning);
           });
}

} // namespace

std::array<int, 8> loopDegrees(LoopOrder order)
{
    return order == LoopOrder::Circle5 ? std::array<int, 8>{1, 4, 7, 3, 6, 2, 5, 1}
                                       : std::array<int, 8>{1, 2, 3, 4, 5, 6, 7, 1};
}

std::array<ShellSlot, 8> shellLoop(PitchClass key, LoopOrder order, const Tuning &tuning)
{
    assert(tuning.stringCount() >= 6);
    auto fromSix = loopFrom(key, order, 6, tuning);
    if (fitsPosition(fromSix, tuning)) {
        return fromSix;
    }
    auto fromFive = loopFrom(key, order, 5, tuning);
    if (fitsPosition(fromFive, tuning)) {
        return fromFive;
    }
    // Neither start fits: keep the 6th-string start and lift any open/negative root an octave (section 2.2 rule 4).
    for (ShellSlot &slot : fromSix) {
        if (slot.root.fret < lowestPlayableFret) {
            slot.root.fret += semitonesPerOctave;
        }
    }
    return fromSix;
}

std::vector<StringFret> shellNotes(const ShellSlot &slot, const Tuning &tuning)
{
    if (slot.root.string == 6) {
        return {slot.root, toneOn(4, slot, ChordTone::Seventh, tuning), toneOn(3, slot, ChordTone::Third, tuning)};
    }
    return {slot.root, toneOn(4, slot, ChordTone::Third, tuning), toneOn(3, slot, ChordTone::Seventh, tuning)};
}

std::array<UpperNote, 2> upperNotes(const ShellSlot &slot, const Tuning &tuning)
{
    // Tension pair per function (section 2.4): I ii IV vi -> 5 + 9, V -> 13 + 9, iii and vii -> fifth + root (9 would
    // be b9).
    ChordTone fifthLike = ChordTone::Fifth;
    ChordTone other = ChordTone::Ninth;
    if (slot.degree == 5) {
        fifthLike = ChordTone::Thirteenth;
    } else if (slot.degree == 3 || slot.degree == 7) {
        other = ChordTone::Root;
    }
    // Root on string 6: string 2 takes the fifth-like tone, string 1 the other; root on string 5 flips them.
    const bool rootOnSix = slot.root.string == 6;
    const ChordTone onString2 = rootOnSix ? fifthLike : other;
    const ChordTone onString1 = rootOnSix ? other : fifthLike;
    return {UpperNote{toneOn(2, slot, onString2, tuning), onString2},
            UpperNote{toneOn(1, slot, onString1, tuning), onString1}};
}

} // namespace cc::theory
