// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/score.h"

#include <cassert>

namespace cc::model
{

namespace
{

/// The value in force at `measureIndex`: the initial one, overridden by every change up to and including there.
template<typename T, typename Member>
T valueAt(const std::vector<Measure> &measures, std::size_t measureIndex, T initial, Member member)
{
    assert(measureIndex < measures.size());
    T current = initial;
    for (std::size_t i = 0; i <= measureIndex; ++i) {
        current = (measures[i].*member).value_or(current);
    }
    return current;
}

} // namespace

const Track &Score::primaryTrack() const
{
    assert(!tracks.empty());
    return tracks.front();
}

Track &Score::primaryTrack()
{
    assert(!tracks.empty());
    return tracks.front();
}

TimeSignature Score::timeSigAt(std::size_t measureIndex) const
{
    return valueAt(measures, measureIndex, initialTimeSig, &Measure::timeSig);
}

KeySignature Score::keySigAt(std::size_t measureIndex) const
{
    return valueAt(measures, measureIndex, initialKeySig, &Measure::keySig);
}

double Score::tempoAt(std::size_t measureIndex) const
{
    return valueAt(measures, measureIndex, initialTempoBpm, &Measure::tempoBpm);
}

double Score::swingAt(std::size_t measureIndex) const
{
    return valueAt(measures, measureIndex, initialSwingRatio, &Measure::swingRatio);
}

std::optional<std::size_t> Score::sectionIndexAt(std::size_t measureIndex) const
{
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].startMeasure > measureIndex) {
            break;
        }
        found = i;
    }
    return found;
}

Tick Score::measureStartTick(std::size_t measureIndex) const
{
    assert(measureIndex <= measures.size());
    Tick start = 0;
    TimeSignature current = initialTimeSig;
    for (std::size_t i = 0; i < measureIndex; ++i) {
        current = measures[i].timeSig.value_or(current);
        start += measureTicks(current);
    }
    return start;
}

Tick Score::totalTicks() const
{
    return measureStartTick(measures.size());
}

} // namespace cc::model
