// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/score.h"

#include <cassert>

namespace cc::model
{

TimeSignature Score::timeSigAt(std::size_t measureIndex) const
{
    assert(measureIndex < measures.size());
    TimeSignature current = initialTimeSig;
    for (std::size_t i = 0; i <= measureIndex; ++i) {
        current = measures[i].timeSig.value_or(current);
    }
    return current;
}

double Score::tempoAt(std::size_t measureIndex) const
{
    assert(measureIndex < measures.size());
    double current = initialTempoBpm;
    for (std::size_t i = 0; i <= measureIndex; ++i) {
        current = measures[i].tempoBpm.value_or(current);
    }
    return current;
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
