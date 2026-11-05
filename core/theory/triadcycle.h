// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/chord.h"
#include "core/theory/tuning.h"

#include <array>
#include <cstdint>

// Routine module 03 "triad cycle": diatonic triads cycled by seconds, thirds or fourths on three strings, closed or
// open voicing. Rules and expected values: docs/spec/routine-spec.md sections 4.2-4.3.
namespace cc::theory
{

enum class TriadCycle : std::uint8_t {
    Seconds, // 2-7: I ii iii IV V vi vii I  (parallel shapes, "map")
    Thirds, // 3-6: I vi IV ii vii V iii I  (two common tones, one note moves)
    Fourths, // 4-5: I IV vii iii vi ii V I  (one common tone, two notes move)
};

enum class Direction : std::uint8_t {
    Up,
    Down, // the same path reversed (section 4.2)
};

enum class Inversion : std::uint8_t {
    Root,
    First,
    Second,
    Auto, // picked per string set so that C lands near frets 3-5 (section 4.3)
};

enum class StringSet : std::uint8_t {
    Strings123,
    Strings234,
    Strings345,
    Strings456,
};

struct TriadVoicing {
    int degree; // 1..7
    PitchClass rootPitchClass;
    ChordQuality quality;
    std::array<StringFret, 3> notes; // lowest string (bottom) first
};

using TriadSequence = std::array<TriadVoicing, 8>;

struct ClosedCycleOptions {
    StringSet strings = StringSet::Strings123;
    Inversion inversion = Inversion::Auto;
    TriadCycle cycle = TriadCycle::Seconds;
    Direction direction = Direction::Up;
};

/// Lowest fret a start voicing may use: open strings are never part of slot 1 (user decision 2026-11-03,
/// routine-spec.md section 4.3), so a shape that would need one starts an octave higher.
constexpr int minStartFret = 1;

/// Highest fret a voicing may use; a slot that climbs past it drops an octave (the fretboard shows 0..22).
constexpr int highestTriadFret = 22;

/// Frets one hand covers in the open 3-6 cycle: the middle voice sits on string 2 (set 4-2-1) unless that would
/// stretch the chord over more frets, then the same pitch is fingered on string 3 (set 4-3-1). Section 4.3.
constexpr int openHandBoxFrets = 4;

/// Degree sequence for a cycle and direction (8 entries, first and last are I).
std::array<int, 8> cycleDegrees(TriadCycle cycle, Direction direction);

/// Notes that change between two consecutive slots: 3 for seconds, 1 for thirds, 2 for fourths.
int movingNotesPerStep(TriadCycle cycle);

/// Closed voicings on three adjacent strings. Slot 1 is the key's I chord in the chosen inversion at the lowest
/// position; every next slot keeps common tones and moves the others the shortest way up on their own strings.
TriadSequence closedTriadCycle(PitchClass key, const ClosedCycleOptions &options, const Tuning &tuning);

/// Open voicings. Seconds: parallel shapes on strings 5-4-2 (root, fifth, third). Thirds: one voice moves per step
/// from the second-inversion I on strings 4-2-1, the middle voice re-fingered on string 3 when the hand box would
/// exceed openHandBoxFrets; slot 8 rewinds to the start chord (section 4.3). Fourths is not an open option.
TriadSequence openTriadCycle(PitchClass key, TriadCycle cycle, Direction direction, const Tuning &tuning);

} // namespace cc::theory
