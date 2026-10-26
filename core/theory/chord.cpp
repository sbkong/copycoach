// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/chord.h"

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

} // namespace

std::string chordName(PitchClass root, ChordQuality quality)
{
    return std::string(flatName(root)) + std::string(suffix(quality));
}

} // namespace cc::theory
