// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/duration.h"

#include <cstdint>
#include <vector>

// The one place ticks and samples meet (PLAN 4-2 principle 7, score-data-path.md section 3). Routine scores give
// piecewise-constant tempo; copy sync points will add segments the same way.
namespace cc::model
{

using SampleIndex = std::int64_t;

struct Score;

struct TempoSegment {
    Tick startTick;
    SampleIndex startSample;
    double bpm;
};

class TimeMap
{
public:
    TimeMap(double sampleRate, std::vector<TempoSegment> segments);

    /// Constant-or-changing tempo per measure, read from the score.
    static TimeMap fromScore(const Score &score, double sampleRate);

    [[nodiscard]] SampleIndex tickToSample(Tick tick) const;
    [[nodiscard]] Tick sampleToTick(SampleIndex sample) const;
    [[nodiscard]] double sampleRate() const;
    [[nodiscard]] const std::vector<TempoSegment> &segments() const;

private:
    [[nodiscard]] const TempoSegment &segmentForTick(Tick tick) const;
    [[nodiscard]] const TempoSegment &segmentForSample(SampleIndex sample) const;
    [[nodiscard]] double samplesPerTick(double bpm) const;

    double m_sampleRate;
    std::vector<TempoSegment> m_segments; // ascending; the first starts at tick 0, sample 0
};

} // namespace cc::model
