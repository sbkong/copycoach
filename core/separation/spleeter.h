// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"
#include "core/separation/stems.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <stop_token>

// Spleeter 4stems (copy-spec.md section 4 "스템", Deezer, MIT): the magnitude of the lowest 1536 bins of
// each 512-frame split goes through the exported graph (four U-Nets and the mask formula inside), the masks
// multiply the complex spectrum (bins above 1536 stay zero: the 16 kHz variant with "zeros" extension),
// and the inverse STFT overlap-adds into the stems. Splits stream: a split's output is final once the next
// one is in, so the sink gets the song in 11.9 s pieces.
namespace cc::separation
{

constexpr std::size_t spleeterT = 512; // frames per split (resources/4stems.json)
constexpr std::size_t spleeterBins = 1536; // F of 4stems-16kHz.json

/// Interleaved stereo 44.1 kHz in, four stems out through the sink. Cancelled between splits.
[[nodiscard]] Result<void> separateSpleeter(std::span<const float> interleaved,
                                            const std::filesystem::path &model,
                                            const StemSink &sink,
                                            const std::stop_token &stop,
                                            const jobs::Report &report);

} // namespace cc::separation
