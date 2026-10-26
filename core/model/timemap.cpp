// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/timemap.h"

#include "core/model/score.h"
#include "core/theory/constants.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace cc::model
{

namespace
{

constexpr double secondsPerMinute = 60.0;

double samplesPerTickAt(double sampleRate, double bpm)
{
    return sampleRate * secondsPerMinute / (bpm * static_cast<double>(theory::ticksPerQuarter));
}

} // namespace

TimeMap::TimeMap(double sampleRate, std::vector<TempoSegment> segments)
    : m_sampleRate(sampleRate)
    , m_segments(std::move(segments))
{
    assert(!m_segments.empty() && m_segments.front().startTick == 0 && m_segments.front().startSample == 0);
}

TimeMap TimeMap::fromScore(const Score &score, double sampleRate)
{
    std::vector<TempoSegment> segments{TempoSegment{0, 0, score.initialTempoBpm}};
    for (std::size_t i = 0; i < score.measures.size(); ++i) {
        const double tempo = score.measures[i].tempoBpm.value_or(segments.back().bpm);
        if (tempo == segments.back().bpm) {
            continue;
        }
        const Tick tick = score.measureStartTick(i);
        const TempoSegment &previous = segments.back();
        const double perTick = samplesPerTickAt(sampleRate, previous.bpm);
        const auto sample =
            previous.startSample + std::llround(static_cast<double>(tick - previous.startTick) * perTick);
        segments.push_back(TempoSegment{tick, sample, tempo});
    }
    return {sampleRate, std::move(segments)};
}

double TimeMap::samplesPerTick(double bpm) const
{
    return samplesPerTickAt(m_sampleRate, bpm);
}

const TempoSegment &TimeMap::segmentForTick(Tick tick) const
{
    const auto it = std::ranges::upper_bound(m_segments, tick, {}, &TempoSegment::startTick);
    return *std::prev(it);
}

const TempoSegment &TimeMap::segmentForSample(SampleIndex sample) const
{
    const auto it = std::ranges::upper_bound(m_segments, sample, {}, &TempoSegment::startSample);
    return *std::prev(it);
}

SampleIndex TimeMap::tickToSample(Tick tick) const
{
    const TempoSegment &segment = segmentForTick(tick);
    return segment.startSample
        + std::llround(static_cast<double>(tick - segment.startTick) * samplesPerTick(segment.bpm));
}

Tick TimeMap::sampleToTick(SampleIndex sample) const
{
    const TempoSegment &segment = segmentForSample(sample);
    return segment.startTick
        + std::llround(static_cast<double>(sample - segment.startSample) / samplesPerTick(segment.bpm));
}

double TimeMap::sampleRate() const
{
    return m_sampleRate;
}

const std::vector<TempoSegment> &TimeMap::segments() const
{
    return m_segments;
}

} // namespace cc::model
