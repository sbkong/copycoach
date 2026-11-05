// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/logmel.h"

#include "core/dsp/fft.h"

#include <array>
#include <cmath>
#include <fstream>
#include <numbers>

namespace cc::predict
{

namespace
{

constexpr float logMultiplier = 1000.0F;

std::array<float, logMelFftSize> periodicHann()
{
    std::array<float, logMelFftSize> window{};
    for (std::size_t i = 0; i < logMelFftSize; ++i) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(logMelFftSize);
        window[i] = static_cast<float>(0.5 - (0.5 * std::cos(phase)));
    }
    return window;
}

/// Sample at a position of the reflect-padded signal (pad = n_fft / 2 on both sides, torch "reflect").
float reflected(std::span<const float> signal, std::ptrdiff_t index)
{
    const auto n = static_cast<std::ptrdiff_t>(signal.size());
    if (n == 0) {
        return 0.0F;
    }
    if (index < 0) {
        index = -index;
    }
    if (index >= n) {
        index = (2 * (n - 1)) - index;
    }
    if (index < 0 || index >= n) {
        return 0.0F; // signals shorter than the pad: nothing sensible to reflect
    }
    return signal[static_cast<std::size_t>(index)];
}

/// Mel bins of one centred frame: windowed magnitudes / sqrt(n_fft) through the filterbank, then log1p(1000 x).
void melFrame(std::span<const float> magnitude, std::span<const float> fb, std::span<float> out)
{
    const float scale = 1.0F / std::sqrt(static_cast<float>(logMelFftSize));
    for (std::size_t m = 0; m < logMelBins; ++m) {
        float sum = 0.0F;
        for (std::size_t k = 0; k < logMelSpectrumBins; ++k) {
            sum += magnitude[k] * scale * fb[(k * logMelBins) + m];
        }
        out[m] = std::log1p(logMultiplier * sum);
    }
}

} // namespace

Result<void> LogMelFilterbank::load(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::FileNotFound, path.string());
    }
    std::vector<float> matrix(logMelSpectrumBins * logMelBins);
    file.read(reinterpret_cast<char *>(matrix.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) raw floats
              static_cast<std::streamsize>(matrix.size() * sizeof(float)));
    if (static_cast<std::size_t>(file.gcount()) != matrix.size() * sizeof(float)
        || file.peek() != std::ifstream::traits_type::eof()) {
        return fail(ErrorCode::FileCorrupt, path.string(), "mel-filterbank.bin is not 513 x 128 floats");
    }
    m_matrix = std::move(matrix);
    return {};
}

bool LogMelFilterbank::loaded() const
{
    return !m_matrix.empty();
}

std::span<const float> LogMelFilterbank::matrix() const
{
    return m_matrix;
}

LogMel logMel(std::span<const float> signal, const LogMelFilterbank &filterbank)
{
    static const std::array<float, logMelFftSize> window = periodicHann();
    const dsp::Fft fft(logMelFftSize);
    LogMel out;
    out.frames = 1 + (signal.size() / logMelHop);
    out.values.assign(out.frames * logMelBins, 0.0F);
    std::vector<float> frame(logMelFftSize);
    std::vector<float> magnitude(logMelSpectrumBins);
    const std::span<const float> fb = filterbank.matrix();
    const auto pad = static_cast<std::ptrdiff_t>(logMelFftSize / 2);
    for (std::size_t f = 0; f < out.frames; ++f) {
        const auto start = static_cast<std::ptrdiff_t>(f * logMelHop) - pad;
        for (std::size_t i = 0; i < logMelFftSize; ++i) {
            frame[i] = reflected(signal, start + static_cast<std::ptrdiff_t>(i)) * window[i];
        }
        fft.magnitudes(frame, magnitude);
        melFrame(magnitude, fb, std::span(out.values).subspan(f * logMelBins, logMelBins));
    }
    return out;
}

} // namespace cc::predict
