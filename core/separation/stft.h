// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/dsp/fft.h"

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

// Spleeter's STFT pair (spleeter/model/__init__.py, tf.signal.stft / inverse_stft): frame 4096, step 1024,
// periodic Hann on both sides, frame_length zeros in front of the signal and zero padding at the end
// (pad_end), the inverse scaled by 2/3 (the Hann-squared overlap at a quarter hop) and the leading
// frame_length samples dropped. One frame at a time, so the caller can stream.
namespace cc::separation
{

constexpr std::size_t stftFrame = 4096;
constexpr std::size_t stftHop = 1024;
constexpr std::size_t stftBins = (stftFrame / 2) + 1;
constexpr float stftWindowCompensation = 2.0F / 3.0F;

class Stft
{
public:
    Stft();

    /// Frames tf.signal.stft(pad_end) makes of a signal with stftFrame zeros in front: ceil((n + 4096) / 1024).
    [[nodiscard]] static std::size_t frameCount(std::size_t samples);
    /// Spectrum of frame `index` of the zero-fronted signal: samples [index * hop - 4096, index * hop) of
    /// `channel`, zeros outside it. `out` holds stftBins values.
    void analyze(std::span<const float> channel, std::size_t index, std::span<std::complex<float>> out) const;
    /// The windowed inverse of one spectrum, already scaled by 2/3: stftFrame samples to overlap-add at
    /// index * hop of the zero-fronted output.
    void synthesize(std::span<const std::complex<float>> spectrum, std::span<float> frame) const;

private:
    dsp::Fft m_fft;
    std::vector<float> m_window;
};

} // namespace cc::separation
