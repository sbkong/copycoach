// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/dsp/fft.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace cc::dsp
{

Fft::Fft(std::size_t size)
    : m_size(size)
    , m_reversed(size)
    , m_twiddles(size / 2)
{
    assert(size >= 2 && (size & (size - 1)) == 0);
    std::size_t bits = 0;
    while ((std::size_t{1} << bits) < size) {
        ++bits;
    }
    for (std::size_t i = 0; i < size; ++i) {
        std::size_t r = 0;
        for (std::size_t b = 0; b < bits; ++b) {
            r |= ((i >> b) & 1U) << (bits - 1 - b);
        }
        m_reversed[i] = r;
    }
    for (std::size_t k = 0; k < size / 2; ++k) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size);
        m_twiddles[k] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
    }
}

std::size_t Fft::size() const
{
    return m_size;
}

void Fft::butterflies(std::span<std::complex<float>> data, std::size_t len) const
{
    const std::size_t half = len / 2;
    const std::size_t step = m_size / len;
    for (std::size_t start = 0; start < m_size; start += len) {
        for (std::size_t k = 0; k < half; ++k) {
            const std::complex<float> u = data[start + k];
            const std::complex<float> v = data[start + k + half] * m_twiddles[k * step];
            data[start + k] = u + v;
            data[start + k + half] = u - v;
        }
    }
}

void Fft::forward(std::span<std::complex<float>> data) const
{
    assert(data.size() == m_size);
    for (std::size_t i = 0; i < m_size; ++i) {
        if (i < m_reversed[i]) {
            std::swap(data[i], data[m_reversed[i]]);
        }
    }
    for (std::size_t len = 2; len <= m_size; len <<= 1U) {
        butterflies(data, len);
    }
}

void Fft::magnitudes(std::span<const float> frame, std::span<float> out) const
{
    assert(out.size() >= (m_size / 2) + 1);
    std::vector<std::complex<float>> data(m_size);
    const std::size_t n = std::min(frame.size(), m_size);
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = {frame[i], 0.0F};
    }
    forward(data);
    for (std::size_t k = 0; k <= m_size / 2; ++k) {
        out[k] = std::abs(data[k]);
    }
}

} // namespace cc::dsp
