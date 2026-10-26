// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <optional>
#include <string_view>

// Pitch classes and MIDI numbers, conventions in coding-standards.md section 17 (0 = C, MIDI 60 = C4).
namespace cc::theory
{

using Midi = int;
using PitchClass = int; // 0..11

constexpr int semitonesPerOctave = 12;

/// Wraps any semitone count into 0..11.
constexpr PitchClass wrapPitchClass(int semitones)
{
    return ((semitones % semitonesPerOctave) + semitonesPerOctave) % semitonesPerOctave;
}

constexpr PitchClass pitchClassOf(Midi midi)
{
    return wrapPitchClass(midi);
}

/// Semitones going up from `from` to `to`, 0..11.
constexpr int intervalUp(PitchClass from, PitchClass to)
{
    return wrapPitchClass(to - from);
}

constexpr PitchClass transpose(PitchClass pitchClass, int semitones)
{
    return wrapPitchClass(pitchClass + semitones);
}

/// "C", "C#", "Db", "Bb" ... -> pitch class. Anything else -> nullopt.
std::optional<PitchClass> parsePitchClass(std::string_view name);

/// Flat spelling for display and tests: 0 -> "C", 1 -> "Db", ... 11 -> "B".
std::string_view flatName(PitchClass pitchClass);

/// Sharp spelling: 1 -> "C#", 3 -> "D#" ...
std::string_view sharpName(PitchClass pitchClass);

} // namespace cc::theory
