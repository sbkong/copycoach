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
#include <vector>

// HT-Demucs through a waveform-in, stems-out ONNX graph (copy-spec.md section 4 "스템 분리 조사", path (나)):
// mix [1, 2, 343980] -> stems [1, 4, 2, 343980] in the model's order drums, bass, other, vocals, with the
// STFT and the normalisation inside the graph. The host does demucs.apply.apply_model's splitting: 7.8 s
// segments, 25 % overlap, triangular transition weights, no random shifts. The weights are research-only:
// the app never downloads or ships this file; it runs whatever the user converted into models/htdemucs/.
namespace cc::separation
{

constexpr std::size_t htdemucsSegment = 343980; // 7.8 s at 44.1 kHz, the transformer's limit
constexpr double htdemucsOverlap = 0.25;
constexpr std::size_t htdemucsStride = static_cast<std::size_t>((1.0 - htdemucsOverlap) * htdemucsSegment);

/// apply.py's transition weight: 1..segment/2 then segment/2..1, divided by the maximum.
[[nodiscard]] std::vector<float> htdemucsWeights(std::size_t segment = htdemucsSegment);

/// Interleaved stereo 44.1 kHz in, four stems out through the sink (in stemNames order). Cancelled between
/// segments.
[[nodiscard]] Result<void> separateHtdemucs(std::span<const float> interleaved,
                                            const std::filesystem::path &model,
                                            const StemSink &sink,
                                            const std::stop_token &stop,
                                            const jobs::Report &report);

} // namespace cc::separation
