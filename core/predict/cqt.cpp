// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/cqt.h"

#include <cmath>
#include <numbers>

namespace cc::predict
{

namespace
{

constexpr double centsPerOctave = 1200.0;

/// One bin's complex Hann kernel, split into real and imaginary parts, with where it starts relative to
/// the frame centre and the factor that turns the raw dot product into librosa's scaled magnitude.
struct Kernel {
    std::ptrdiff_t first = 0; // offset of re[0] from the frame centre
    std::vector<float> re;
    std::vector<float> im;
    float scale = 0.0F; // sqrt(length) / sum(window)
};

/// Sample offsets floor(-length / 2) .. floor(length / 2) - 1, as np.arange(-length // 2, length // 2).
Kernel makeKernel(double freq)
{
    const double r = std::pow(2.0, 1.0 / static_cast<double>(cqtBinsPerOctave));
    const double alpha = ((r * r) - 1.0) / ((r * r) + 1.0);
    const double length = cqtSampleRate / (alpha * freq);
    Kernel k;
    k.first = static_cast<std::ptrdiff_t>(std::floor(-length / 2.0));
    const auto last = static_cast<std::ptrdiff_t>(std::floor(length / 2.0));
    const auto n = static_cast<std::size_t>(last - k.first);
    k.re.resize(n);
    k.im.resize(n);
    double windowSum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double window =
            0.5 - (0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n)));
        const double phase = -2.0 * std::numbers::pi * freq
            * static_cast<double>(k.first + static_cast<std::ptrdiff_t>(i)) / cqtSampleRate;
        k.re[i] = static_cast<float>(window * std::cos(phase));
        k.im[i] = static_cast<float>(window * std::sin(phase));
        windowSum += window;
    }
    k.scale = static_cast<float>(std::sqrt(length) / windowSum);
    return k;
}

std::vector<Kernel> makeKernels(double tuningCents)
{
    std::vector<Kernel> kernels;
    kernels.reserve(cqtBins);
    const double fmin = cqtFminHz * std::pow(2.0, tuningCents / centsPerOctave);
    for (std::size_t bin = 0; bin < cqtBins; ++bin) {
        const double freq = fmin * std::pow(2.0, static_cast<double>(bin) / static_cast<double>(cqtBinsPerOctave));
        kernels.push_back(makeKernel(freq));
    }
    return kernels;
}

/// log(|C| + floor) of one bin at the frame centred on `centre`; samples outside the signal count as zero.
float response(const Kernel &k, std::span<const float> signal, std::ptrdiff_t centre)
{
    const std::ptrdiff_t start = centre + k.first;
    const auto n = static_cast<std::ptrdiff_t>(k.re.size());
    const std::ptrdiff_t lo = std::max<std::ptrdiff_t>(start, 0);
    const std::ptrdiff_t hi = std::min<std::ptrdiff_t>(start + n, static_cast<std::ptrdiff_t>(signal.size()));
    float re = 0.0F;
    float im = 0.0F;
    for (std::ptrdiff_t t = lo; t < hi; ++t) {
        const auto j = static_cast<std::size_t>(t - start);
        const float x = signal[static_cast<std::size_t>(t)];
        re += k.re[j] * x;
        im += k.im[j] * x;
    }
    return std::log((k.scale * std::hypot(re, im)) + cqtLogFloor);
}

} // namespace

Result<LogCqt>
logCqt(std::span<const float> mono22k, double tuningCents, const std::stop_token &stop, const jobs::Report &report)
{
    const std::vector<Kernel> kernels = makeKernels(tuningCents);
    LogCqt out;
    out.frames = 1 + (mono22k.size() / cqtHop);
    out.values.resize(out.frames * cqtBins);
    for (std::size_t frame = 0; frame < out.frames; ++frame) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, "cqt");
        }
        const auto centre = static_cast<std::ptrdiff_t>(frame * cqtHop);
        for (std::size_t bin = 0; bin < cqtBins; ++bin) {
            out.values[(frame * cqtBins) + bin] = response(kernels[bin], mono22k, centre);
        }
        report(static_cast<double>(frame + 1) / static_cast<double>(out.frames));
    }
    return out;
}

} // namespace cc::predict
