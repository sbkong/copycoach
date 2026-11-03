// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/duration.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The one place ticks and samples meet (PLAN 4-2 principle 7, score-data-path.md section 3). Two inputs
// (copy-spec.md section 16.2 A2): the score's per-measure tempo, and, for a recording, sync points that pin a
// tick to a second of that audio version. Between sync points the effective tempo is whatever makes them meet;
// after the last one the score tempo takes over again. Built per device sample rate; rebuild when it changes.
// The RT clock (rt::TickClock) uses the same samples-per-tick formula and rounding, so both agree sample-exact.
namespace cc::model
{

using SampleIndex = std::int64_t;

struct Score;

struct TempoSegment {
    Tick startTick;
    SampleIndex startSample; // may be negative: the score can start before the audio does
    double bpm; // effective quarter notes per minute from startTick on
};

/// "Absolute tick `tick` sounds at `seconds` of the audio" (copy-spec.md section 1 `sync_points[]`, one list
/// per audio version). A valid list is strictly ascending in both fields.
struct SyncPoint {
    Tick tick;
    double seconds;
};

/// Index of the first point that breaks the ascending order, or nullopt when the list is valid.
std::optional<std::size_t> firstInvalidSyncPoint(std::span<const SyncPoint> points);

class TimeMap
{
public:
    TimeMap(double sampleRate, std::vector<TempoSegment> segments);

    /// Constant-or-changing tempo per measure, read from the score (the routine, and a copy project with no
    /// sync points yet).
    static TimeMap fromScore(const Score &score, double sampleRate);
    /// Sync points pin the grid to a recording; `points` must pass firstInvalidSyncPoint. Empty = fromScore.
    static TimeMap fromSyncPoints(const Score &score, std::span<const SyncPoint> points, double sampleRate);

    [[nodiscard]] SampleIndex tickToSample(Tick tick) const;
    [[nodiscard]] Tick sampleToTick(SampleIndex sample) const;
    [[nodiscard]] double tickToSeconds(Tick tick) const;
    [[nodiscard]] Tick secondsToTick(double seconds) const;
    [[nodiscard]] double sampleRate() const;
    [[nodiscard]] const std::vector<TempoSegment> &segments() const;

private:
    [[nodiscard]] const TempoSegment &segmentForTick(Tick tick) const;
    [[nodiscard]] const TempoSegment &segmentForSample(SampleIndex sample) const;
    [[nodiscard]] double samplesPerTick(double bpm) const;

    double m_sampleRate;
    std::vector<TempoSegment> m_segments; // ascending; the first starts at tick 0
};

} // namespace cc::model
