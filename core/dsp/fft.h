// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

// A plain radix-2 FFT for the feature front ends (power-of-two sizes only). Not for the audio callback.
namespace cc::dsp
{

class Fft
{
public:
    explicit Fft(std::size_t size); // a power of two

    [[nodiscard]] std::size_t size() const;
    /// In-place complex transform.
    void forward(std::span<std::complex<float>> data) const;
    /// Magnitudes of the first size/2 + 1 bins of a real frame (zero-padded or truncated to size).
    void magnitudes(std::span<const float> frame, std::span<float> out) const;

private:
    /// One stage of the iterative Cooley-Tukey transform: butterflies of span `len`.
    void butterflies(std::span<std::complex<float>> data, std::size_t len) const;

    std::size_t m_size;
    std::vector<std::size_t> m_reversed; // bit-reversal permutation
    std::vector<std::complex<float>> m_twiddles;
};

} // namespace cc::dsp
