// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/chord.h"

#include <array>
#include <optional>
#include <set>

namespace cc::theory
{

namespace
{

std::string_view suffix(ChordQuality quality)
{
    switch (quality) {
    case ChordQuality::Major:
        return "";
    case ChordQuality::Minor:
        return "m";
    case ChordQuality::Diminished:
        return "dim";
    case ChordQuality::Maj7:
        return "maj7";
    case ChordQuality::Min7:
        return "m7";
    case ChordQuality::Dom7:
        return "7";
    case ChordQuality::Min7b5:
        return "m7b5";
    }
    return "";
}

constexpr std::array<ChordQuality, 7> allQualities{ChordQuality::Major,
                                                   ChordQuality::Minor,
                                                   ChordQuality::Diminished,
                                                   ChordQuality::Maj7,
                                                   ChordQuality::Min7,
                                                   ChordQuality::Dom7,
                                                   ChordQuality::Min7b5};

bool hasSeventh(ChordQuality quality)
{
    return quality == ChordQuality::Maj7 || quality == ChordQuality::Min7 || quality == ChordQuality::Dom7
        || quality == ChordQuality::Min7b5;
}

std::set<PitchClass> tonesOf(PitchClass root, ChordQuality quality)
{
    std::set<PitchClass> tones{root,
                               transpose(root, chordToneInterval(quality, ChordTone::Third)),
                               transpose(root, chordToneInterval(quality, ChordTone::Fifth))};
    if (hasSeventh(quality)) {
        tones.insert(transpose(root, chordToneInterval(quality, ChordTone::Seventh)));
    }
    return tones;
}

/// The quality that spells `wanted` from `root`, if any.
std::optional<ChordQuality> qualityOf(PitchClass root, const std::set<PitchClass> &wanted)
{
    for (const ChordQuality quality : allQualities) {
        if (tonesOf(root, quality) == wanted) {
            return quality;
        }
    }
    return std::nullopt;
}

} // namespace

std::string chordName(PitchClass root, ChordQuality quality)
{
    return std::string(flatName(root)) + std::string(suffix(quality));
}

std::string nameChordOf(std::span<const PitchClass> pitchClasses)
{
    const std::set<PitchClass> wanted(pitchClasses.begin(), pitchClasses.end());
    for (const PitchClass root : wanted) {
        if (const auto quality = qualityOf(root, wanted)) {
            return chordName(root, *quality);
        }
    }
    return {};
}

} // namespace cc::theory
