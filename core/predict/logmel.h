// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

// The log-mel front end Beat This! was trained on (copy-spec.md section 4 "박 예측 조사"): 22050 Hz mono,
// n_fft 1024, hop 441 (50 fps), periodic Hann, centred with reflect padding, |X| / sqrt(n_fft), the
// torchaudio mel matrix shipped as mel-filterbank.bin ([513, 128] float32), then log1p(1000 x). Checked
// against beat_this.preprocessing.LogMelSpect to 1e-5 (tools/models/export_beat_this.py).
namespace cc::predict
{

constexpr int logMelSampleRate = 22050;
constexpr std::size_t logMelFftSize = 1024;
constexpr std::size_t logMelHop = 441;
constexpr std::size_t logMelBins = 128;
constexpr std::size_t logMelSpectrumBins = (logMelFftSize / 2) + 1;
constexpr double logMelFps = static_cast<double>(logMelSampleRate) / static_cast<double>(logMelHop);

/// Frames x 128 bins, row-major, as the model's "spectrogram" input wants them.
struct LogMel {
    std::vector<float> values;
    std::size_t frames = 0;
};

class LogMelFilterbank
{
public:
    /// Loads mel-filterbank.bin; FileCorrupt when the size is not 513 x 128 floats.
    [[nodiscard]] Result<void> load(const std::filesystem::path &path);
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] std::span<const float> matrix() const; // [513][128]

private:
    std::vector<float> m_matrix;
};

/// The whole signal (mono, 22050 Hz) to frames; 1 + floor(n / hop) of them, like torchaudio with center.
LogMel logMel(std::span<const float> signal, const LogMelFilterbank &filterbank);

} // namespace cc::predict
