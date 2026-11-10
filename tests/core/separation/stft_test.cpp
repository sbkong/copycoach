// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Spleeter's STFT pair (copy-spec.md section 4 "스템"): analysis frames overlap-added through the inverse give
// the signal back (the 2/3 factor is exact for a periodic Hann at a quarter hop), the frame count follows
// tf.signal.stft(pad_end) over the zero-fronted signal, and the FFT inverse undoes the forward.
#include "core/dsp/fft.h"
#include "core/separation/stft.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace cc;
using namespace cc::separation;

TEST_CASE("fft: inverse undoes forward")
{
    const dsp::Fft fft(16);
    std::vector<std::complex<float>> data(16);
    for (std::size_t i = 0; i < 16; ++i) {
        data[i] = {static_cast<float>(i) * 0.25F, static_cast<float>(i % 3)};
    }
    const std::vector<std::complex<float>> original = data;
    fft.forward(data);
    fft.inverse(data);
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(std::abs(data[i] - original[i]) < 1e-4F);
    }
}

TEST_CASE("stft: frames overlap-add back to the signal")
{
    constexpr std::size_t n = 20000;
    std::mt19937 rng(3); // NOLINT(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed) fixed fixture
    std::uniform_real_distribution<float> dist(-0.5F, 0.5F);
    std::vector<float> signal(n);
    for (std::size_t i = 0; i < n; ++i) {
        signal[i] = dist(rng)
            + static_cast<float>(0.3 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / 44100.0));
    }
    const Stft stft;
    const std::size_t frames = Stft::frameCount(n);
    CHECK(frames == (n + stftFrame + stftHop - 1) / stftHop); // 24 frames for 20000 + 4096 samples
    std::vector<float> out(((frames - 1) * stftHop) + stftFrame, 0.0F);
    std::vector<std::complex<float>> spectrum(stftBins);
    std::vector<float> frame(stftFrame);
    for (std::size_t f = 0; f < frames; ++f) {
        stft.analyze(signal, f, spectrum);
        stft.synthesize(spectrum, frame);
        for (std::size_t i = 0; i < stftFrame; ++i) {
            out[(f * stftHop) + i] += frame[i];
        }
    }
    float worst = 0.0F;
    for (std::size_t i = 0; i < n; ++i) { // the output starts stftFrame samples late (the zero front)
        worst = std::max(worst, std::abs(out[stftFrame + i] - signal[i]));
    }
    CHECK(worst < 1e-4F);
    CHECK(std::abs(out[100]) < 1e-5F); // nothing before the song
}
