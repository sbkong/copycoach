// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/locate.h"

namespace cc::model
{

namespace
{

/// The first beat of the voice in the measure starting after `afterTick` (measure-relative), or nullptr.
const Beat *nextInMeasure(const Measure &measure, std::size_t track, int voice, Tick afterTick)
{
    for (const Beat &beat : measure.beats) {
        if (beat.track == track && beat.voice == voice && beat.tick > afterTick) {
            return &beat;
        }
    }
    return nullptr;
}

/// The last beat of the voice in the measure starting at or before `atTick` (measure-relative), or nullptr.
const Beat *lastAtOrBefore(const Measure &measure, std::size_t track, int voice, Tick atTick)
{
    const Beat *found = nullptr;
    for (const Beat &beat : measure.beats) {
        if (beat.track == track && beat.voice == voice && beat.tick <= atTick) {
            found = &beat;
        }
    }
    return found;
}

} // namespace

std::optional<std::size_t> measureAtTick(const Score &score, Tick absTick)
{
    if (absTick < 0 || score.measures.empty()) {
        return std::nullopt;
    }
    Tick start = 0;
    for (std::size_t m = 0; m < score.measures.size(); ++m) {
        const Tick end = start + measureTicks(score.timeSigAt(m));
        if (absTick < end) {
            return m;
        }
        start = end;
    }
    return std::nullopt;
}

std::optional<LocatedBeat> locateBeat(const Score &score, Tick absTick, std::size_t track, int voice)
{
    const auto here = measureAtTick(score, absTick);
    if (!here) {
        return std::nullopt;
    }
    for (std::size_t m = *here + 1; m-- > 0;) {
        const Tick measureStart = score.measureStartTick(m);
        const Tick inMeasure = m == *here ? absTick - measureStart : measureTicks(score.timeSigAt(m));
        const Beat *beat = lastAtOrBefore(score.measures[m], track, voice, inMeasure);
        if (beat == nullptr) {
            continue;
        }
        LocatedBeat located{m, beat, measureStart + beat->tick, measureStart + measureTicks(score.timeSigAt(m))};
        if (const Beat *next = nextInMeasure(score.measures[m], track, voice, beat->tick); next != nullptr) {
            located.nextTick = measureStart + next->tick;
        }
        return located;
    }
    return std::nullopt;
}

} // namespace cc::model
