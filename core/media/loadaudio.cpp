// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/loadaudio.h"

#include "core/media/audiofile.h"
#include "core/media/resampler.h"

#include <algorithm>
#include <cmath>

namespace cc::media
{

namespace
{

constexpr std::size_t chunkFrames = 65536;

Result<std::vector<float>> readAll(AudioFileReader &reader, const std::stop_token &stop, const jobs::Report &report)
{
    const AudioFileInfo &info = reader.info();
    const auto channels = static_cast<std::size_t>(info.channels);
    std::vector<float> interleaved(static_cast<std::size_t>(info.frames) * channels);
    std::size_t filled = 0;
    while (filled < interleaved.size()) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, "decode");
        }
        const std::size_t want = std::min(chunkFrames * channels, interleaved.size() - filled);
        const std::int64_t got = reader.read(std::span(interleaved).subspan(filled, want));
        if (got <= 0) {
            break;
        }
        filled += static_cast<std::size_t>(got) * channels;
        report(static_cast<double>(filled) / static_cast<double>(interleaved.size()));
    }
    interleaved.resize(filled);
    return interleaved;
}

/// Every output channel is the average of the input channels (mono) or input channel c, the last one
/// repeated when the input has fewer (a mono file spread to stereo).
std::vector<float> toChannels(std::span<const float> interleaved, int inChannels, int outChannels)
{
    const auto in = static_cast<std::size_t>(inChannels);
    const auto out = static_cast<std::size_t>(outChannels);
    const std::size_t frames = interleaved.size() / in;
    std::vector<float> result(frames * out);
    for (std::size_t f = 0; f < frames; ++f) {
        float sum = 0.0F;
        for (std::size_t c = 0; c < in; ++c) {
            sum += interleaved[(f * in) + c];
        }
        for (std::size_t c = 0; c < out; ++c) {
            result[(f * out) + c] =
                out == 1 ? sum / static_cast<float>(in) : interleaved[(f * in) + std::min(c, in - 1)];
        }
    }
    return result;
}

Result<std::vector<float>> resample(std::span<const float> interleaved, int channels, double ratio)
{
    Resampler resampler(channels);
    const auto ch = static_cast<std::size_t>(channels);
    const std::size_t frames = interleaved.size() / ch;
    std::vector<float> out((static_cast<std::size_t>(std::ceil(static_cast<double>(frames) * ratio)) + chunkFrames)
                           * ch);
    std::size_t produced = 0;
    std::size_t consumed = 0;
    while (true) {
        const std::span<const float> input = interleaved.subspan(consumed);
        const std::span<float> output = std::span(out).subspan(produced);
        if (output.size() < ch) {
            out.resize(out.size() + (chunkFrames * ch));
            continue;
        }
        Result<Resampler::Progress> step = resampler.process(input, output, ratio, true);
        if (!step) {
            return std::unexpected(step.error());
        }
        consumed += static_cast<std::size_t>(step->inputUsed) * ch;
        produced += static_cast<std::size_t>(step->outputProduced) * ch;
        if (step->outputProduced == 0) {
            break;
        }
    }
    out.resize(produced);
    return out;
}

} // namespace

Result<std::vector<float>> loadResampled(const std::filesystem::path &audio,
                                         double rate,
                                         int channels,
                                         const std::stop_token &stop,
                                         const jobs::Report &report)
{
    AudioFileReader reader;
    if (Result<void> r = reader.open(audio); !r) {
        return std::unexpected(r.error());
    }
    Result<std::vector<float>> interleaved = readAll(reader, stop, report);
    if (!interleaved) {
        return std::unexpected(interleaved.error());
    }
    const AudioFileInfo &info = reader.info();
    std::vector<float> frames =
        info.channels == channels ? std::move(*interleaved) : toChannels(*interleaved, info.channels, channels);
    if (info.sampleRate == rate) {
        return frames;
    }
    return resample(frames, channels, rate / info.sampleRate);
}

} // namespace cc::media
