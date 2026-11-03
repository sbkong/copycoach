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

SampleIndex sampleAfter(const TempoSegment &segment, Tick tick, double sampleRate)
{
    return segment.startSample
        + std::llround(static_cast<double>(tick - segment.startTick) * samplesPerTickAt(sampleRate, segment.bpm));
}

/// The tempo that makes `from` and `to` meet: quarter notes between them per minute between them.
double effectiveBpm(const SyncPoint &from, const SyncPoint &to)
{
    const double quarters = static_cast<double>(to.tick - from.tick) / static_cast<double>(theory::ticksPerQuarter);
    return quarters * secondsPerMinute / (to.seconds - from.seconds);
}

/// Index of the measure that contains `tick` (the last one when the tick lies past the end).
std::size_t measureContaining(const Score &score, Tick tick)
{
    std::size_t index = 0;
    while (index + 1 < score.measures.size() && score.measureStartTick(index + 1) <= tick) {
        ++index;
    }
    return index;
}

/// Score tempo changes at measure starts after `fromTick`, continuing the last segment.
void appendScoreTempoFrom(std::vector<TempoSegment> &segments, const Score &score, Tick fromTick, double sampleRate)
{
    for (std::size_t i = 0; i < score.measures.size(); ++i) {
        const Tick tick = score.measureStartTick(i);
        const double tempo = score.tempoAt(i);
        if (tick < fromTick || tempo == segments.back().bpm) {
            continue;
        }
        segments.push_back(TempoSegment{tick, sampleAfter(segments.back(), tick, sampleRate), tempo});
    }
}

} // namespace

std::optional<std::size_t> firstInvalidSyncPoint(std::span<const SyncPoint> points)
{
    for (std::size_t i = 0; i < points.size(); ++i) {
        const bool ascending =
            i == 0 || (points[i].tick > points[i - 1].tick && points[i].seconds > points[i - 1].seconds);
        if (!ascending || points[i].seconds < 0.0 || points[i].tick < 0) {
            return i;
        }
    }
    return std::nullopt;
}

TimeMap::TimeMap(double sampleRate, std::vector<TempoSegment> segments)
    : m_sampleRate(sampleRate)
    , m_segments(std::move(segments))
{
    assert(!m_segments.empty() && m_segments.front().startTick == 0);
}

TimeMap TimeMap::fromScore(const Score &score, double sampleRate)
{
    const double first = score.measures.empty() ? score.initialTempoBpm : score.tempoAt(0);
    std::vector<TempoSegment> segments{TempoSegment{0, 0, first}};
    appendScoreTempoFrom(segments, score, 0, sampleRate);
    return {sampleRate, std::move(segments)};
}

TimeMap TimeMap::fromSyncPoints(const Score &score, std::span<const SyncPoint> points, double sampleRate)
{
    if (points.empty()) {
        return fromScore(score, sampleRate);
    }
    assert(!firstInvalidSyncPoint(points));
    const Tick lastTick = points.back().tick;
    const double tailBpm =
        score.measures.empty() ? score.initialTempoBpm : score.tempoAt(measureContaining(score, lastTick));
    const double firstBpm = points.size() > 1 ? effectiveBpm(points[0], points[1]) : tailBpm;
    const auto sampleOf = [sampleRate](double seconds) {
        return std::llround(seconds * sampleRate);
    };
    std::vector<TempoSegment> segments;
    segments.reserve(points.size() + 1);
    if (points.front().tick > 0) {
        // The score starts before the first pin: extrapolate back at the first segment's tempo.
        const TempoSegment first{points.front().tick, sampleOf(points.front().seconds), firstBpm};
        segments.push_back(TempoSegment{0, sampleAfter(first, 0, sampleRate), firstBpm});
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        segments.push_back(
            TempoSegment{points[i].tick, sampleOf(points[i].seconds), effectiveBpm(points[i], points[i + 1])});
    }
    segments.push_back(TempoSegment{lastTick, sampleOf(points.back().seconds), tailBpm});
    appendScoreTempoFrom(segments, score, lastTick, sampleRate);
    return {sampleRate, std::move(segments)};
}

double TimeMap::samplesPerTick(double bpm) const
{
    return samplesPerTickAt(m_sampleRate, bpm);
}

const TempoSegment &TimeMap::segmentForTick(Tick tick) const
{
    const auto it = std::ranges::upper_bound(m_segments, tick, {}, &TempoSegment::startTick);
    return it == m_segments.begin() ? m_segments.front() : *std::prev(it);
}

const TempoSegment &TimeMap::segmentForSample(SampleIndex sample) const
{
    const auto it = std::ranges::upper_bound(m_segments, sample, {}, &TempoSegment::startSample);
    return it == m_segments.begin() ? m_segments.front() : *std::prev(it);
}

SampleIndex TimeMap::tickToSample(Tick tick) const
{
    return sampleAfter(segmentForTick(tick), tick, m_sampleRate);
}

Tick TimeMap::sampleToTick(SampleIndex sample) const
{
    const TempoSegment &segment = segmentForSample(sample);
    return segment.startTick
        + std::llround(static_cast<double>(sample - segment.startSample) / samplesPerTick(segment.bpm));
}

double TimeMap::tickToSeconds(Tick tick) const
{
    return static_cast<double>(tickToSample(tick)) / m_sampleRate;
}

Tick TimeMap::secondsToTick(double seconds) const
{
    return sampleToTick(std::llround(seconds * m_sampleRate));
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
