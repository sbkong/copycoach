// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/tuning.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>

namespace cc::theory
{

Tuning Tuning::standardSix()
{
    return Tuning{{64, 59, 55, 50, 45, 40}, 0};
}

int Tuning::stringCount() const
{
    return static_cast<int>(strings.size());
}

Midi Tuning::openPitch(int string) const
{
    assert(string >= 1 && string <= stringCount());
    return strings[static_cast<std::size_t>(string - 1)];
}

Midi Tuning::pitchAt(StringFret position) const
{
    return openPitch(position.string) + capo + position.fret;
}

PitchClass Tuning::pitchClassAt(StringFret position) const
{
    return pitchClassOf(pitchAt(position));
}

int Tuning::nearestFret(int string, PitchClass pitchClass, int nearFret, int maxFret) const
{
    assert(maxFret >= semitonesPerOctave - 1);
    const int first =
        intervalUp(pitchClassOf(openPitch(string) + capo), pitchClass); // lowest fret with that pitch class
    int best = first;
    for (int fret = first + semitonesPerOctave; fret <= maxFret; fret += semitonesPerOctave) {
        if (std::abs(fret - nearFret) < std::abs(best - nearFret)) {
            best = fret;
        }
    }
    return best;
}

} // namespace cc::theory
