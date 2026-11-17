// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/dsp/fft.h"
#include "core/error.h"
#include "core/jobs/job.h"
#include "core/separation/stems.h"

#include <complex>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

// BS-RoFormer-SW, six stems (copy-spec.md section 4 "스템 모델 재조사 2026-11-17", decision A): the graph takes
// the complex STFT of one 13.35 s chunk and returns complex masks per stem; the host does the STFT pair the
// way torch.stft does (n_fft 2048, hop 512, periodic Hann, centred with reflect padding) and the chunk
// overlap-add of MSST's demix (step chunk/2, linear fades of chunk/10, reflect-padded borders). The user
// converts the file with tools/models/export_bs_roformer.py; its weights have no known provenance, so the
// app never downloads or ships it.
namespace cc::separation
{

constexpr std::string_view roformerFileName = "bs_roformer.onnx";
constexpr std::size_t roformerChunk = 588800; // config audio.chunk_size
constexpr std::size_t roformerFft = 2048;
constexpr std::size_t roformerHop = 512;
constexpr std::size_t roformerBins = (roformerFft / 2) + 1;
constexpr std::size_t roformerFrames = 1 + (roformerChunk / roformerHop); // torch.stft with center: 1151
constexpr std::size_t roformerStep = roformerChunk / 2; // inference.num_overlap 2
constexpr std::size_t roformerFade = roformerChunk / 10;

/// torch.stft / torch.istft with the model's settings, one whole chunk at a time.
class RoformerStft
{
public:
    RoformerStft();

    /// roformerChunk samples -> spectrum [frame][bin], roformerFrames * roformerBins values.
    void analyze(std::span<const float> samples, std::span<std::complex<float>> spectrum) const;
    /// The inverse, normalised by the window envelope: spectrum [frame][bin] -> roformerChunk samples.
    void synthesize(std::span<const std::complex<float>> spectrum, std::span<float> samples) const;

private:
    /// The centred signal: reflect padding of half a frame on both sides.
    [[nodiscard]] static float centred(std::span<const float> samples, std::ptrdiff_t index);

    dsp::Fft m_fft;
    std::vector<float> m_window;
    std::vector<float> m_envelope; // summed squared windows over the padded length
};

/// demix's chunk weight: a linear fade in and out over roformerFade, 1 between; the song's first chunk keeps
/// its head at 1 and the last chunk its tail.
[[nodiscard]] std::vector<float> roformerWeights(bool first, bool last);

/// Interleaved stereo 44.1 kHz in, six stems out through the sink (in stemNames order). Cancelled between
/// chunks.
[[nodiscard]] Result<void> separateRoformer(std::span<const float> interleaved,
                                            const std::filesystem::path &model,
                                            const StemSink &sink,
                                            const std::stop_token &stop,
                                            const jobs::Report &report);

} // namespace cc::separation
