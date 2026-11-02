// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/pitch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Chord qualities, diatonic functions and chord tones, all computed from formulas (PLAN 4-2 principle 3).
namespace cc::theory
{

enum class ChordQuality : std::uint8_t {
    Major,
    Minor,
    Diminished,
    Maj7,
    Min7,
    Dom7,
    Min7b5,
};

/// Chord tones by role; intervals in semitones above the root.
enum class ChordTone : std::uint8_t {
    Root,
    Third,
    Fifth,
    Seventh,
    Ninth,
    Thirteenth,
    FlatNinth, // b9 and b13: tensions of a secondary dominant resolving to a minor chord
    FlatThirteenth,
};

constexpr int degreesPerScale = 7;

constexpr std::size_t degreeIndex(int degree) // degree 1..7 (8 wraps to 1)
{
    return static_cast<std::size_t>((degree - 1) % degreesPerScale);
}

constexpr int majorScaleStep(int degree) // semitones above the key
{
    constexpr std::array<int, degreesPerScale> steps{0, 2, 4, 5, 7, 9, 11};
    return steps[degreeIndex(degree)];
}

constexpr PitchClass degreePitchClass(PitchClass key, int degree)
{
    return transpose(key, majorScaleStep(degree));
}

constexpr ChordQuality diatonicSeventh(int degree)
{
    constexpr std::array<ChordQuality, degreesPerScale> qualities{ChordQuality::Maj7,
                                                                  ChordQuality::Min7,
                                                                  ChordQuality::Min7,
                                                                  ChordQuality::Maj7,
                                                                  ChordQuality::Dom7,
                                                                  ChordQuality::Min7,
                                                                  ChordQuality::Min7b5};
    return qualities[degreeIndex(degree)];
}

constexpr ChordQuality diatonicTriad(int degree)
{
    constexpr std::array<ChordQuality, degreesPerScale> qualities{ChordQuality::Major,
                                                                  ChordQuality::Minor,
                                                                  ChordQuality::Minor,
                                                                  ChordQuality::Major,
                                                                  ChordQuality::Major,
                                                                  ChordQuality::Minor,
                                                                  ChordQuality::Diminished};
    return qualities[degreeIndex(degree)];
}

constexpr bool hasMinorThird(ChordQuality quality)
{
    return quality == ChordQuality::Minor || quality == ChordQuality::Diminished || quality == ChordQuality::Min7
        || quality == ChordQuality::Min7b5;
}

constexpr bool hasFlatFifth(ChordQuality quality)
{
    return quality == ChordQuality::Diminished || quality == ChordQuality::Min7b5;
}

/// Semitones above the root for a chord tone of this quality (ninth and thirteenth are the natural tensions).
constexpr int chordToneInterval(ChordQuality quality, ChordTone tone)
{
    switch (tone) {
    case ChordTone::Root:
        return 0;
    case ChordTone::Third:
        return hasMinorThird(quality) ? 3 : 4;
    case ChordTone::Fifth:
        return hasFlatFifth(quality) ? 6 : 7;
    case ChordTone::Seventh:
        return quality == ChordQuality::Maj7 ? 11 : 10;
    case ChordTone::Ninth:
        return 2;
    case ChordTone::Thirteenth:
        return 9;
    case ChordTone::FlatNinth:
        return 1;
    case ChordTone::FlatThirteenth:
        return 8;
    }
    return 0;
}

/// Whether a pitch class belongs to the major scale of `key`.
constexpr bool inMajorKey(PitchClass key, PitchClass pitchClass)
{
    const int interval = intervalUp(key, pitchClass);
    for (int degree = 1; degree <= degreesPerScale; ++degree) {
        if (majorScaleStep(degree) == interval) {
            return true;
        }
    }
    return false;
}

/// Display name such as "Cmaj7", "Dm7", "G7", "Bm7b5", "C", "Dm", "Bdim" (flat spelling).
std::string chordName(PitchClass root, ChordQuality quality);

/// The chord these pitch classes spell, trying each of them as the root ("E G B D" -> "Em7"); empty when no
/// triad or seventh chord fits. Used for the "upper four" of a shell voicing (routine-spec.md section 2.7).
std::string nameChordOf(std::span<const PitchClass> pitchClasses);

} // namespace cc::theory
