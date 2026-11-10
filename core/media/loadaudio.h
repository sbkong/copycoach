// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"

#include <filesystem>
#include <stop_token>
#include <vector>

// A whole recording as float frames at the rate and channel count an engine wants (the prediction front
// ends at mono 22050 Hz, stem separation at stereo 44100 Hz). Decoded in chunks with the cancel token checked
// between them, mixed down (average) or spread (mono copied to both sides), then resampled.
namespace cc::media
{

/// Interleaved frames; FileNotFound / FileCorrupt from the reader, Cancelled between chunks. `report` runs 0..1
/// over the decode.
[[nodiscard]] Result<std::vector<float>> loadResampled(const std::filesystem::path &audio,
                                                       double rate,
                                                       int channels,
                                                       const std::stop_token &stop,
                                                       const jobs::Report &report);

} // namespace cc::media
