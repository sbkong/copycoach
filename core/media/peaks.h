// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <vector>

namespace cc::media
{

/// A waveform overview: the peak absolute sample of every bucket, mixed down to one channel, 0..1.
struct Peaks {
    std::vector<float> buckets;
    double seconds = 0.0;
    double sampleRate = 0.0;
};

/// Reads the whole file once in chunks (a job: cancel through `stop`, progress through `report`) and folds
/// it into `bucketCount` peaks. Cancelled when the token asks, FileNotFound / FileCorrupt from the reader.
Result<Peaks> computePeaks(const std::filesystem::path &path,
                           std::size_t bucketCount,
                           const std::stop_token &stop,
                           const jobs::Report &report);

} // namespace cc::media
