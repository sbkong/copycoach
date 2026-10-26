// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/constants.h"

#include <cmath>
#include <cstdint>

// Sample <-> tick mapping for the current tempo segment (audio-engine.md section 3). The callback only ever
// needs the segment it is in: a tempo change or a seek rebases the origin, nothing is allocated. The UI-side
// history of segments is model::TimeMap; both use the same samples-per-tick formula and rounding.
namespace cc::rt
{

class TickClock
{
public:
    TickClock(double sampleRate, double bpm)
        : m_sampleRate(sampleRate)
    {
        setTempo(bpm);
    }

    /// Samples per tick at the current tempo.
    [[nodiscard]] double samplesPerTick() const
    {
        return m_samplesPerTick;
    }

    [[nodiscard]] std::int64_t sampleAt(std::int64_t tick) const
    {
        return m_sampleOrigin + std::llround(static_cast<double>(tick - m_tickOrigin) * m_samplesPerTick);
    }

    /// The tick that starts at or before `sample` (floor).
    [[nodiscard]] std::int64_t tickAt(std::int64_t sample) const
    {
        const double ticks = static_cast<double>(sample - m_sampleOrigin) / m_samplesPerTick;
        return m_tickOrigin + static_cast<std::int64_t>(std::floor(ticks));
    }

    /// Tempo change from `sample` onward: the segment starts here at the tick this sample maps to.
    void setTempoAt(std::int64_t sample, double bpm)
    {
        const std::int64_t tick = tickAt(sample);
        m_sampleOrigin = sample;
        m_tickOrigin = tick;
        setTempo(bpm);
    }

    /// Declares that `sample` is tick `tick` (seek, loop wrap, count-in start).
    void rebase(std::int64_t sample, std::int64_t tick)
    {
        m_sampleOrigin = sample;
        m_tickOrigin = tick;
    }

    [[nodiscard]] double bpm() const
    {
        return m_bpm;
    }

private:
    void setTempo(double bpm)
    {
        m_bpm = bpm;
        m_samplesPerTick = m_sampleRate * 60.0 / (bpm * static_cast<double>(theory::ticksPerQuarter));
    }

    double m_sampleRate;
    double m_bpm = 0.0;
    double m_samplesPerTick = 0.0;
    std::int64_t m_sampleOrigin = 0;
    std::int64_t m_tickOrigin = 0;
};

} // namespace cc::rt
