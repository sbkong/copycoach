// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/pitch.h"

#include <array>
#include <cstddef>

namespace cc::theory
{

namespace
{

constexpr std::array<std::string_view, 12> flatNames{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
constexpr std::array<std::string_view, 12> sharpNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<int, 7> naturalPitchClass{9, 11, 0, 2, 4, 5, 7}; // A B C D E F G

} // namespace

std::optional<PitchClass> parsePitchClass(std::string_view name)
{
    if (name.empty() || name.size() > 2) {
        return std::nullopt;
    }
    const char letter = name[0];
    if (letter < 'A' || letter > 'G') {
        return std::nullopt;
    }
    int pitchClass = naturalPitchClass[static_cast<std::size_t>(letter - 'A')];
    if (name.size() == 2) {
        if (name[1] == '#') {
            pitchClass += 1;
        } else if (name[1] == 'b') {
            pitchClass -= 1;
        } else {
            return std::nullopt;
        }
    }
    return wrapPitchClass(pitchClass);
}

std::string_view flatName(PitchClass pitchClass)
{
    return flatNames[static_cast<std::size_t>(wrapPitchClass(pitchClass))];
}

std::string_view sharpName(PitchClass pitchClass)
{
    return sharpNames[static_cast<std::size_t>(wrapPitchClass(pitchClass))];
}

} // namespace cc::theory
