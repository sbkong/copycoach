// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>

// Long work off the main thread (copy-spec.md section 16.2 B-4, states.md section 2): stem separation, model
// downloads, media extraction and copies, measurements, predictions, pre-rendering, exports. A job is a function
// that polls its stop token and reports a fraction; the queue turns that into events the UI shows as
// "ratio + time left" (or busy + elapsed when the fraction is unknown) with a cancel that answers at once.
namespace cc::jobs
{

using JobId = std::uint64_t;

enum class JobKind : std::uint8_t {
    StemSeparation,
    ModelDownload,
    MediaExtract,
    MediaCopy,
    Measure,
    Prediction,
    Prerender,
    Export,
    Waveform, // peaks of a recording for the audio track
};

enum class JobState : std::uint8_t {
    Queued,
    Running,
    Finished,
};

struct Progress {
    std::optional<double> fraction; // 0..1 when the job reports one
    std::chrono::milliseconds elapsed{0};
    std::optional<std::chrono::milliseconds> remaining; // estimated from the fraction once it is meaningful
};

/// Below this fraction the remaining time is not estimated (the first percent says little about the rest).
constexpr double minFractionForEstimate = 0.05;

std::optional<std::chrono::milliseconds> estimateRemaining(std::optional<double> fraction,
                                                           std::chrono::milliseconds elapsed);

/// One step of a job's life. `error` is set only for a failed Finished (Cancelled and Timeout included).
struct JobEvent {
    JobId id = 0;
    JobKind kind = JobKind::StemSeparation;
    JobState state = JobState::Queued;
    Progress progress;
    std::optional<Error> error;
};

/// Called by the work as it goes, with the fraction done (0..1).
using Report = std::function<void(double fraction)>;
/// The work itself: returns fail(ErrorCode::Cancelled, ...) when it stops because the token asked it to.
using Work = std::function<Result<void>(std::stop_token stop, const Report &report)>;

} // namespace cc::jobs
