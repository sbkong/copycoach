// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/separation/htdemucs.h"

#include "core/predict/ortsession.h"

#include <algorithm>
#include <array>

namespace cc::separation
{

namespace
{

constexpr const char *htdemucsInput = "mix";
constexpr std::array<const char *, 1> htdemucsOutputs{"stems"};
constexpr std::array<std::size_t, stemCount> canonicalOf{1, 2, 3, 0}; // model index -> stemNames index

/// One separation: segments weighted into accumulators that emit a stride's worth of frames once the next
/// segment can no longer touch them.
class HtdemucsRun
{
public:
    HtdemucsRun(std::span<const float> interleaved, const StemSink &sink)
        : m_input(interleaved)
        , m_sink(&sink)
        , m_samples(interleaved.size() / stemChannels)
        , m_weights(htdemucsWeights())
        , m_mix(static_cast<std::size_t>(stemChannels) * htdemucsSegment)
        , m_weightSum(htdemucsSegment, 0.0F)
    {
        for (std::vector<float> &acc : m_acc) {
            acc.assign(htdemucsSegment * stemChannels, 0.0F);
        }
    }

    [[nodiscard]] Result<void>
    run(const std::filesystem::path &model, const std::stop_token &stop, const jobs::Report &report)
    {
        predict::OrtSession session;
        if (Result<void> r = session.open(model); !r) {
            return std::unexpected(r.error());
        }
        const std::size_t segments = std::max<std::size_t>(1, (m_samples + htdemucsStride - 1) / htdemucsStride);
        constexpr std::array<std::int64_t, 3> shape{1, stemChannels, static_cast<std::int64_t>(htdemucsSegment)};
        for (std::size_t s = 0; s < segments; ++s) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled, model.string());
            }
            fillMix(s * htdemucsStride);
            if (Result<void> r = session.run(htdemucsInput, shape, m_mix, htdemucsOutputs, m_outputs); !r) {
                return std::unexpected(r.error());
            }
            accumulate();
            if (Result<void> r = emit(s * htdemucsStride, s + 1 == segments); !r) {
                return r;
            }
            report(static_cast<double>(s + 1) / static_cast<double>(segments));
        }
        return {};
    }

private:
    /// Planar [channel][sample] of the segment starting at `offset`, zeros past the song.
    void fillMix(std::size_t offset)
    {
        for (std::size_t c = 0; c < stemChannels; ++c) {
            for (std::size_t i = 0; i < htdemucsSegment; ++i) {
                const std::size_t t = offset + i;
                m_mix[(c * htdemucsSegment) + i] = t < m_samples ? m_input[(t * stemChannels) + c] : 0.0F;
            }
        }
    }

    /// stems [1, 4, 2, segment] times the transition weight into the accumulators.
    void accumulate()
    {
        for (std::size_t m = 0; m < stemCount; ++m) {
            for (std::size_t c = 0; c < stemChannels; ++c) {
                accumulateChannel(m, c);
            }
        }
        for (std::size_t i = 0; i < htdemucsSegment; ++i) {
            m_weightSum[i] += m_weights[i];
        }
    }

    void accumulateChannel(std::size_t modelStem, std::size_t channel)
    {
        const std::vector<float> &stems = m_outputs[0];
        std::vector<float> &acc = m_acc[canonicalOf[modelStem]];
        const std::size_t base = ((modelStem * stemChannels) + channel) * htdemucsSegment;
        for (std::size_t i = 0; i < htdemucsSegment; ++i) {
            acc[(i * stemChannels) + channel] += stems[base + i] * m_weights[i];
        }
    }

    /// Frames [offset, offset + stride) are final after this segment (everything when it is the last one):
    /// divide by the summed weights, hand them to the sink, slide the buffers by a stride.
    [[nodiscard]] Result<void> emit(std::size_t offset, bool last)
    {
        const std::size_t count =
            std::min(last ? htdemucsSegment : htdemucsStride, m_samples - std::min(offset, m_samples));
        std::vector<float> out(count * stemChannels);
        for (std::size_t k = 0; k < stemCount; ++k) {
            normalised(m_acc[k], count, out);
            if (Result<void> r = count > 0 ? (*m_sink)(k, out) : Result<void>{}; !r) {
                return r;
            }
            slide(m_acc[k], htdemucsStride * stemChannels);
        }
        slide(m_weightSum, htdemucsStride);
        return {};
    }

    /// The first `count` frames of an accumulator divided by the summed weights.
    void normalised(const std::vector<float> &acc, std::size_t count, std::vector<float> &out) const
    {
        for (std::size_t i = 0; i < count; ++i) {
            const float w = std::max(m_weightSum[i], 1e-6F);
            for (std::size_t c = 0; c < stemChannels; ++c) {
                out[(i * stemChannels) + c] = acc[(i * stemChannels) + c] / w;
            }
        }
    }

    static void slide(std::vector<float> &buffer, std::size_t by)
    {
        std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(by), buffer.end(), buffer.begin());
        std::fill(buffer.end() - static_cast<std::ptrdiff_t>(by), buffer.end(), 0.0F);
    }

    std::span<const float> m_input;
    const StemSink *m_sink;
    std::size_t m_samples;
    std::vector<float> m_weights;
    std::vector<float> m_mix; // the model input, planar
    std::vector<std::vector<float>> m_outputs;
    std::array<std::vector<float>, stemCount> m_acc; // interleaved, one segment each
    std::vector<float> m_weightSum;
};

} // namespace

std::vector<float> htdemucsWeights(std::size_t segment)
{
    std::vector<float> weights(segment);
    const std::size_t half = segment / 2;
    for (std::size_t i = 0; i < segment; ++i) {
        const std::size_t w = i < half ? i + 1 : segment - i;
        weights[i] = static_cast<float>(w) / static_cast<float>(half);
    }
    return weights;
}

Result<void> separateHtdemucs(std::span<const float> interleaved,
                              const std::filesystem::path &model,
                              const StemSink &sink,
                              const std::stop_token &stop,
                              const jobs::Report &report)
{
    HtdemucsRun run(interleaved, sink);
    return run.run(model, stop, report);
}

} // namespace cc::separation
