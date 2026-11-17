// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/separation/roformer.h"

#include "core/predict/ortsession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace cc::separation
{

namespace
{

constexpr const char *roformerInput = "spec";
constexpr std::array<const char *, 1> roformerOutputs{"masks"};
constexpr std::array<std::size_t, maxStemCount> canonicalOf{2, 1, 3, 0, 4, 5}; // bass drums other vocals guitar piano
constexpr std::size_t half = roformerFft / 2;
constexpr std::size_t padded = roformerChunk + roformerFft; // the centred chunk
constexpr std::size_t border = roformerChunk - roformerStep; // demix's reflect padding around the song
constexpr std::size_t packed =
    2 * static_cast<std::size_t>(stemChannels) * roformerBins; // last axis: (bin, channel, re/im)
constexpr std::size_t binCount = roformerFrames * roformerBins;
constexpr float minWeight = 1e-6F;

/// Reflect padding: the sample `distance` past an edge is the one `distance` inside it.
std::size_t reflected(std::size_t length, std::ptrdiff_t index)
{
    if (index < 0) {
        return static_cast<std::size_t>(-index);
    }
    const auto last = static_cast<std::ptrdiff_t>(length) - 1;
    return index > last ? static_cast<std::size_t>((2 * last) - index) : static_cast<std::size_t>(index);
}

/// One separation: the planar input with demix's borders, the chunk's spectra and masks, and per-stem
/// accumulators that emit a step's worth of frames once the next chunk can no longer touch them.
class RoformerRun
{
public:
    RoformerRun(std::span<const float> interleaved, const StemSink &sink)
        : m_sink(&sink)
        , m_samples(interleaved.size() / stemChannels)
        , m_pad(m_samples > 2 * border ? border : 0)
        , m_length(m_samples + (2 * m_pad))
        , m_spec(roformerFrames * packed)
        , m_counter(roformerChunk, 0.0F)
        , m_masked(binCount)
        , m_synth(roformerChunk)
    {
        for (std::size_t c = 0; c < stemChannels; ++c) {
            m_channels[c].resize(m_samples);
            for (std::size_t i = 0; i < m_samples; ++i) {
                m_channels[c][i] = interleaved[(i * stemChannels) + c];
            }
            m_chunk[c].assign(roformerChunk, 0.0F);
            m_spectra[c].assign(binCount, {});
        }
        for (std::vector<float> &acc : m_acc) {
            acc.assign(roformerChunk * stemChannels, 0.0F);
        }
    }

    [[nodiscard]] Result<void>
    run(const std::filesystem::path &model, const std::stop_token &stop, const jobs::Report &report)
    {
        predict::OrtSession session;
        if (Result<void> r = session.open(model); !r) {
            return std::unexpected(r.error());
        }
        const std::size_t chunks = std::max<std::size_t>(1, (m_length + roformerStep - 1) / roformerStep);
        constexpr std::array<std::int64_t, 3> shape{1, roformerFrames, packed};
        for (std::size_t c = 0; c < chunks; ++c) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled, model.string());
            }
            const std::size_t from = c * roformerStep;
            const std::size_t length = std::min(roformerChunk, m_length - from);
            fillChunk(from, length);
            analyze();
            if (Result<void> r = session.run(roformerInput, shape, m_spec, roformerOutputs, m_outputs); !r) {
                return std::unexpected(r.error());
            }
            accumulate(roformerWeights(c == 0, from + roformerChunk >= m_length), length);
            if (Result<void> r = emit(from, c + 1 == chunks ? length : roformerStep); !r) {
                return r;
            }
            report(static_cast<double>(c + 1) / static_cast<double>(chunks));
        }
        return {};
    }

private:
    /// The song with demix's reflect borders (only when it is longer than two of them).
    [[nodiscard]] float bordered(std::size_t channel, std::size_t index) const
    {
        const auto inner = static_cast<std::ptrdiff_t>(index) - static_cast<std::ptrdiff_t>(m_pad);
        return m_channels[channel][reflected(m_samples, inner)];
    }

    /// The chunk starting at `from` of the bordered song; a short last chunk is reflect-padded when more
    /// than half of it is song, zero-padded otherwise (demix).
    void fillChunk(std::size_t from, std::size_t length)
    {
        for (std::size_t c = 0; c < stemChannels; ++c) {
            fillChannel(c, from, length);
        }
    }

    void fillChannel(std::size_t channel, std::size_t from, std::size_t length)
    {
        const bool reflect = length > (roformerChunk / 2) + 1;
        std::vector<float> &chunk = m_chunk[channel];
        for (std::size_t k = 0; k < length; ++k) {
            chunk[k] = bordered(channel, from + k);
        }
        for (std::size_t k = length; k < roformerChunk; ++k) {
            chunk[k] = reflect ? chunk[length - 2 - (k - length)] : 0.0F;
        }
    }

    /// Both channels' spectra, packed as the model wants: [frame][(bin, channel, re/im)].
    void analyze()
    {
        for (std::size_t c = 0; c < stemChannels; ++c) {
            m_stft.analyze(m_chunk[c], m_spectra[c]);
            pack(c);
        }
    }

    void pack(std::size_t channel)
    {
        for (std::size_t t = 0; t < roformerFrames; ++t) {
            for (std::size_t f = 0; f < roformerBins; ++f) {
                const std::complex<float> v = m_spectra[channel][(t * roformerBins) + f];
                const std::size_t at = (t * packed) + (((f * stemChannels) + channel) * 2);
                m_spec[at] = v.real();
                m_spec[at + 1] = v.imag();
            }
        }
    }

    /// masks [1, stem, (bin, channel), frame, re/im] times the spectrum of `channel`, for one model stem.
    void maskStem(std::size_t modelStem, std::size_t channel)
    {
        const std::vector<float> &masks = m_outputs[0];
        for (std::size_t t = 0; t < roformerFrames; ++t) {
            for (std::size_t f = 0; f < roformerBins; ++f) {
                const std::size_t fs = (f * stemChannels) + channel;
                const std::size_t at = ((((modelStem * stemChannels * roformerBins) + fs) * roformerFrames) + t) * 2;
                const std::complex<float> mask{masks[at], masks[at + 1]};
                m_masked[(t * roformerBins) + f] = mask * m_spectra[channel][(t * roformerBins) + f];
            }
        }
    }

    /// Every stem's masked spectrum back to samples, weighted into the accumulators.
    void accumulate(const std::vector<float> &weights, std::size_t length)
    {
        for (std::size_t m = 0; m < maxStemCount; ++m) {
            accumulateStem(m, weights, length);
        }
        for (std::size_t k = 0; k < length; ++k) {
            m_counter[k] += weights[k];
        }
    }

    void accumulateStem(std::size_t modelStem, const std::vector<float> &weights, std::size_t length)
    {
        std::vector<float> &acc = m_acc[canonicalOf[modelStem]];
        for (std::size_t c = 0; c < stemChannels; ++c) {
            maskStem(modelStem, c);
            m_stft.synthesize(m_masked, m_synth);
            for (std::size_t k = 0; k < length; ++k) {
                acc[(k * stemChannels) + c] += m_synth[k] * weights[k];
            }
        }
    }

    /// The first `count` accumulated frames are final: divide by the summed weights, drop the borders, hand
    /// the rest to the sink, slide by a step.
    [[nodiscard]] Result<void> emit(std::size_t from, std::size_t count)
    {
        const std::size_t lo = std::max(from, m_pad); // the part of [from, from + count) inside the song
        const std::size_t hi = std::min(from + count, m_pad + m_samples);
        const std::size_t songFrom = lo < hi ? lo - from : 0;
        const std::size_t songTo = lo < hi ? hi - from : 0;
        std::vector<float> out((songTo - songFrom) * stemChannels);
        for (std::size_t k = 0; k < maxStemCount; ++k) {
            normalised(m_acc[k], songFrom, songTo, out);
            if (Result<void> r = out.empty() ? Result<void>{} : (*m_sink)(k, out); !r) {
                return r;
            }
            slide(m_acc[k], roformerStep * stemChannels);
        }
        slide(m_counter, roformerStep);
        return {};
    }

    /// Frames [from, to) of an accumulator divided by the summed weights.
    void normalised(const std::vector<float> &acc, std::size_t from, std::size_t to, std::vector<float> &out) const
    {
        for (std::size_t i = from; i < to; ++i) {
            const float w = std::max(m_counter[i], minWeight);
            for (std::size_t c = 0; c < stemChannels; ++c) {
                out[((i - from) * stemChannels) + c] = acc[(i * stemChannels) + c] / w;
            }
        }
    }

    static void slide(std::vector<float> &buffer, std::size_t by)
    {
        std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(by), buffer.end(), buffer.begin());
        std::fill(buffer.end() - static_cast<std::ptrdiff_t>(by), buffer.end(), 0.0F);
    }

    const StemSink *m_sink;
    std::size_t m_samples;
    std::size_t m_pad; // demix's border on each side
    std::size_t m_length; // with the borders
    RoformerStft m_stft;
    std::array<std::vector<float>, stemChannels> m_channels;
    std::array<std::vector<float>, stemChannels> m_chunk;
    std::array<std::vector<std::complex<float>>, stemChannels> m_spectra; // [frame][bin]
    std::vector<float> m_spec; // the model input
    std::vector<std::vector<float>> m_outputs;
    std::array<std::vector<float>, maxStemCount> m_acc; // interleaved, one chunk each
    std::vector<float> m_counter;
    std::vector<std::complex<float>> m_masked;
    std::vector<float> m_synth;
};

} // namespace

RoformerStft::RoformerStft()
    : m_fft(roformerFft)
    , m_window(roformerFft)
    , m_envelope(padded, 0.0F)
{
    for (std::size_t n = 0; n < roformerFft; ++n) { // periodic Hann (torch.hann_window)
        m_window[n] = 0.5F
            - (0.5F
               * static_cast<float>(
                   std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / static_cast<double>(roformerFft))));
    }
    for (std::size_t t = 0; t < roformerFrames; ++t) {
        for (std::size_t n = 0; n < roformerFft; ++n) {
            m_envelope[(t * roformerHop) + n] += m_window[n] * m_window[n];
        }
    }
}

float RoformerStft::centred(std::span<const float> samples, std::ptrdiff_t index)
{
    return samples[reflected(samples.size(), index - static_cast<std::ptrdiff_t>(half))];
}

void RoformerStft::analyze(std::span<const float> samples, std::span<std::complex<float>> spectrum) const
{
    std::vector<std::complex<float>> frame(roformerFft);
    for (std::size_t t = 0; t < roformerFrames; ++t) {
        for (std::size_t n = 0; n < roformerFft; ++n) {
            frame[n] = {centred(samples, static_cast<std::ptrdiff_t>((t * roformerHop) + n)) * m_window[n], 0.0F};
        }
        m_fft.forward(frame);
        std::copy_n(frame.begin(), roformerBins, spectrum.begin() + static_cast<std::ptrdiff_t>(t * roformerBins));
    }
}

void RoformerStft::synthesize(std::span<const std::complex<float>> spectrum, std::span<float> samples) const
{
    std::vector<float> sum(padded, 0.0F);
    std::vector<std::complex<float>> frame(roformerFft);
    for (std::size_t t = 0; t < roformerFrames; ++t) {
        const std::span<const std::complex<float>> bins = spectrum.subspan(t * roformerBins, roformerBins);
        for (std::size_t f = 0; f < roformerBins; ++f) {
            frame[f] = bins[f];
        }
        for (std::size_t f = roformerBins; f < roformerFft; ++f) {
            frame[f] = std::conj(bins[roformerFft - f]);
        }
        m_fft.inverse(frame);
        for (std::size_t n = 0; n < roformerFft; ++n) {
            sum[(t * roformerHop) + n] += frame[n].real() * m_window[n];
        }
    }
    for (std::size_t i = 0; i < roformerChunk; ++i) {
        samples[i] = sum[i + half] / std::max(m_envelope[i + half], minWeight);
    }
}

std::vector<float> roformerWeights(bool first, bool last)
{
    std::vector<float> weights(roformerChunk, 1.0F);
    for (std::size_t i = 0; i < roformerFade; ++i) { // torch.linspace(0, 1, fade) and its mirror
        const float ramp = static_cast<float>(i) / static_cast<float>(roformerFade - 1);
        if (!first) {
            weights[i] = ramp;
        }
        if (!last) {
            weights[roformerChunk - 1 - i] = ramp;
        }
    }
    return weights;
}

Result<void> separateRoformer(std::span<const float> interleaved,
                              const std::filesystem::path &model,
                              const StemSink &sink,
                              const std::stop_token &stop,
                              const jobs::Report &report)
{
    RoformerRun run(interleaved, sink);
    return run.run(model, stop, report);
}

} // namespace cc::separation
