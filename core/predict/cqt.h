// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"

#include <cstddef>
#include <span>
#include <stop_token>
#include <vector>

// Constant-Q transform as the chord model's training pipeline made it (librosa 0.11.0 `cqt`, copy-spec.md
// section 4 "코드"): 144 bins from C1, 24 per octave, hop 2048 at 22050 Hz, log(|C| + 1e-6). Computed
// directly, one complex Hann kernel per bin over the full-rate signal (librosa's octave-by-octave
// downsampling and basis sparsification are approximations of this): bin k at f_k = fmin * 2^(k/24) takes
// Q * sr / f_k samples with Q = 1 / alpha, alpha = (2^(2/24) - 1) / (2^(2/24) + 1); the kernel is
// L1-normalised and the response scaled by sqrt(length) (norm=1, scale=True); frame m is centred on m * hop
// over the zero-padded signal (center=True, pad_mode="constant"). Measured against librosa on the fixtures
// (tools/models/export_chordmini.py, 2026-11-10): sounding bins within 0.06 on average, quiet valleys
// between partials differ by up to 3.9 (Hann side lobes the sparsified basis lacks); chord labels agree.
namespace cc::predict
{

constexpr int cqtSampleRate = 22050;
constexpr std::size_t cqtHop = 2048;
constexpr std::size_t cqtBins = 144;
constexpr std::size_t cqtBinsPerOctave = 24;
constexpr double cqtFminHz = 32.70319566257483; // C1 (librosa.note_to_hz)
constexpr double cqtFps = static_cast<double>(cqtSampleRate) / static_cast<double>(cqtHop);
constexpr float cqtLogFloor = 1e-6F;

struct LogCqt {
    std::size_t frames = 0;
    std::vector<float> values; // frames x cqtBins, log(|C| + 1e-6)

    [[nodiscard]] float at(std::size_t frame, std::size_t bin) const
    {
        return values[(frame * cqtBins) + bin];
    }
};

/// 1 + n / hop frames of mono 22050 Hz samples. `tuningCents` shifts every bin by the recording's A4 offset
/// (librosa estimated it per song during training; keycents.h gives ours). Cancelled between frames.
[[nodiscard]] Result<LogCqt>
logCqt(std::span<const float> mono22k, double tuningCents, const std::stop_token &stop, const jobs::Report &report);

} // namespace cc::predict
