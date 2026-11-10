// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/separation/spleeter.h"

#include "core/predict/ortsession.h"
#include "core/separation/stft.h"

#include <algorithm>
#include <array>
#include <complex>
#include <vector>

namespace cc::separation
{

namespace
{

constexpr const char *spleeterInput = "magnitude";
constexpr std::array<const char *, 1> spleeterOutputs{"masks"};
constexpr std::size_t splitSamples = spleeterT * stftHop;
constexpr std::size_t accFrames = splitSamples + stftFrame; // a split's frames reach this far

/// One separation: the planar input, the spectra of the current split, the model and the per-stem
/// overlap-add buffers that emit each split as soon as it is final.
class SpleeterRun
{
public:
    SpleeterRun(std::span<const float> interleaved, const StemSink &sink)
        : m_sink(&sink)
        , m_samples(interleaved.size() / stemChannels)
        , m_spectra(static_cast<std::size_t>(stemChannels) * spleeterT * stftBins)
        , m_magnitude(static_cast<std::size_t>(stemChannels) * spleeterT * spleeterBins)
    {
        for (std::size_t c = 0; c < stemChannels; ++c) {
            m_channels[c].resize(m_samples);
            for (std::size_t i = 0; i < m_samples; ++i) {
                m_channels[c][i] = interleaved[(i * stemChannels) + c];
            }
        }
        for (std::vector<float> &acc : m_acc) {
            acc.assign(accFrames * stemChannels, 0.0F);
        }
    }

    [[nodiscard]] Result<void>
    run(const std::filesystem::path &model, const std::stop_token &stop, const jobs::Report &report)
    {
        predict::OrtSession session;
        if (Result<void> r = session.open(model); !r) {
            return std::unexpected(r.error());
        }
        const std::size_t frames = Stft::frameCount(m_samples);
        const std::size_t splits = (frames + spleeterT - 1) / spleeterT;
        for (std::size_t s = 0; s < splits; ++s) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled, model.string());
            }
            analyzeSplit(s, frames);
            if (Result<void> r = session.run(spleeterInput, shape(), m_magnitude, spleeterOutputs, m_outputs); !r) {
                return std::unexpected(r.error());
            }
            synthesizeSplit();
            if (Result<void> r = emit(s); !r) {
                return r;
            }
            report(static_cast<double>(s + 1) / static_cast<double>(splits));
        }
        return {};
    }

private:
    [[nodiscard]] static std::array<std::int64_t, 4> shape()
    {
        return {1, stemChannels, static_cast<std::int64_t>(spleeterT), static_cast<std::int64_t>(spleeterBins)};
    }

    [[nodiscard]] std::span<std::complex<float>> spectrum(std::size_t channel, std::size_t frame)
    {
        return std::span(m_spectra).subspan(((channel * spleeterT) + frame) * stftBins, stftBins);
    }

    /// Spectra of the split's frames (zeros past the song) and their magnitudes over the model's bins.
    void analyzeSplit(std::size_t split, std::size_t frames)
    {
        for (std::size_t c = 0; c < stemChannels; ++c) {
            for (std::size_t f = 0; f < spleeterT; ++f) {
                analyzeFrame(c, f, (split * spleeterT) + f, frames);
            }
        }
    }

    void analyzeFrame(std::size_t channel, std::size_t frame, std::size_t global, std::size_t frames)
    {
        const std::span<std::complex<float>> spec = spectrum(channel, frame);
        if (global < frames) {
            m_stft.analyze(m_channels[channel], global, spec);
        } else {
            std::ranges::fill(spec, std::complex<float>{});
        }
        for (std::size_t b = 0; b < spleeterBins; ++b) {
            m_magnitude[(((channel * spleeterT) + frame) * spleeterBins) + b] = std::abs(spec[b]);
        }
    }

    /// Every stem's masked spectrum back to samples, overlap-added into its buffer.
    void synthesizeSplit()
    {
        constexpr std::size_t perStem = stemChannels * spleeterT;
        for (std::size_t i = 0; i < stemCount * perStem; ++i) {
            synthesizeFrame(i / perStem, (i / spleeterT) % stemChannels, i % spleeterT);
        }
    }

    /// masks [1, stem, channel, frame, bin] times the spectrum, zero above the model's bins.
    void synthesizeFrame(std::size_t stem, std::size_t channel, std::size_t frame)
    {
        const std::vector<float> &masks = m_outputs[0];
        const std::span<const std::complex<float>> spec = spectrum(channel, frame);
        const std::size_t maskAt = ((((stem * stemChannels) + channel) * spleeterT) + frame) * spleeterBins;
        for (std::size_t b = 0; b < stftBins; ++b) {
            m_masked[b] = b < spleeterBins ? spec[b] * masks[maskAt + b] : std::complex<float>{};
        }
        m_stft.synthesize(m_masked, m_frame);
        addFrame(m_acc[stem], channel, frame * stftHop, m_frame);
    }

    static void addFrame(std::vector<float> &acc, std::size_t channel, std::size_t at, std::span<const float> frame)
    {
        for (std::size_t i = 0; i < stftFrame; ++i) {
            acc[((at + i) * stemChannels) + channel] += frame[i];
        }
    }

    /// The split's first splitSamples frames are final: hand the part inside the song to the sink (the
    /// output starts stftFrame samples late because of the zero front), then slide the buffer.
    [[nodiscard]] Result<void> emit(std::size_t split)
    {
        const std::size_t from = split * splitSamples; // in zero-fronted output samples
        const std::size_t songFrom = from > stftFrame ? from - stftFrame : 0;
        const std::size_t songTo = std::min(m_samples, from + splitSamples - stftFrame);
        const std::size_t skip = from < stftFrame ? stftFrame - from : 0; // frames before the song starts
        const std::size_t count = songTo > songFrom ? (songTo - songFrom) * stemChannels : 0;
        for (std::size_t k = 0; k < stemCount; ++k) {
            const std::span<const float> out = std::span(m_acc[k]).subspan(skip * stemChannels, count);
            if (Result<void> r = out.empty() ? Result<void>{} : (*m_sink)(k, out); !r) {
                return r;
            }
            slide(m_acc[k], splitSamples * stemChannels);
        }
        return {};
    }

    static void slide(std::vector<float> &buffer, std::size_t by)
    {
        std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(by), buffer.end(), buffer.begin());
        std::fill(buffer.end() - static_cast<std::ptrdiff_t>(by), buffer.end(), 0.0F);
    }

    const StemSink *m_sink;
    std::size_t m_samples;
    std::array<std::vector<float>, stemChannels> m_channels;
    Stft m_stft;
    std::vector<std::complex<float>> m_spectra; // [channel][frame][bin] of the current split
    std::vector<float> m_magnitude; // the model input
    std::vector<std::vector<float>> m_outputs;
    std::vector<std::complex<float>> m_masked = std::vector<std::complex<float>>(stftBins);
    std::vector<float> m_frame = std::vector<float>(stftFrame);
    std::array<std::vector<float>, stemCount> m_acc; // interleaved, accFrames each
};

} // namespace

Result<void> separateSpleeter(std::span<const float> interleaved,
                              const std::filesystem::path &model,
                              const StemSink &sink,
                              const std::stop_token &stop,
                              const jobs::Report &report)
{
    SpleeterRun run(interleaved, sink);
    return run.run(model, stop, report);
}

} // namespace cc::separation
