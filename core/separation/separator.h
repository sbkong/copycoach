// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"
#include "core/separation/stems.h"

#include <array>
#include <filesystem>
#include <stop_token>

// The separation job (copy-spec.md section 4 "스템", section 16.2 D-2): decode the recording to stereo
// 44.1 kHz, run the engine the model names, write the four stems as 16-bit FLAC into the project's stems
// folder. Progress: a tenth for the decode, the rest for the engine.
namespace cc::separation
{

/// Decode and separate through the sink.
[[nodiscard]] Result<void> separate(const std::filesystem::path &audio,
                                    const StemModel &model,
                                    const StemSink &sink,
                                    const std::stop_token &stop,
                                    const jobs::Report &report);

/// <outDir>/<stem>.flac for each stem, in stemNames order; partial files are removed on failure.
[[nodiscard]] Result<std::array<std::filesystem::path, stemCount>> separateToFiles(const std::filesystem::path &audio,
                                                                                   const StemModel &model,
                                                                                   const std::filesystem::path &outDir,
                                                                                   const std::stop_token &stop,
                                                                                   const jobs::Report &report);

} // namespace cc::separation
