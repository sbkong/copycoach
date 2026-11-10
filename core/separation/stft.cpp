// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/separation/stft.h"

#include <cmath>
#include <numbers>

namespace cc::separation
{

Stft::Stft()
    : m_fft(stftFrame)
    , m_window(stftFrame)
{
    for (std::size_t i = 0; i < stftFrame; ++i) {
        m_window[i] = static_cast<float>(
            0.5 - (0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(stftFrame))));
    }
}

std::size_t Stft::frameCount(std::size_t samples)
{
    return (samples + stftFrame + stftHop - 1) / stftHop;
}

void Stft::analyze(std::span<const float> channel, std::size_t index, std::span<std::complex<float>> out) const
{
    std::vector<std::complex<float>> data(stftFrame);
    const auto start = static_cast<std::ptrdiff_t>(index * stftHop) - static_cast<std::ptrdiff_t>(stftFrame);
    const auto n = static_cast<std::ptrdiff_t>(channel.size());
    for (std::size_t i = 0; i < stftFrame; ++i) {
        const std::ptrdiff_t t = start + static_cast<std::ptrdiff_t>(i);
        const float x = t >= 0 && t < n ? channel[static_cast<std::size_t>(t)] : 0.0F;
        data[i] = {x * m_window[i], 0.0F};
    }
    m_fft.forward(data);
    for (std::size_t k = 0; k < stftBins; ++k) {
        out[k] = data[k];
    }
}

void Stft::synthesize(std::span<const std::complex<float>> spectrum, std::span<float> frame) const
{
    std::vector<std::complex<float>> data(stftFrame);
    for (std::size_t k = 0; k < stftBins; ++k) {
        data[k] = spectrum[k];
    }
    for (std::size_t k = 1; k + 1 < stftBins; ++k) { // the real signal's mirrored half
        data[stftFrame - k] = std::conj(spectrum[k]);
    }
    m_fft.inverse(data);
    for (std::size_t i = 0; i < stftFrame; ++i) {
        frame[i] = data[i].real() * m_window[i] * stftWindowCompensation;
    }
}

} // namespace cc::separation
