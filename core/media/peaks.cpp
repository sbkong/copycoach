// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/peaks.h"

#include "core/media/audiofile.h"

#include <algorithm>
#include <cmath>

namespace cc::media
{

namespace
{

constexpr std::int64_t chunkFrames = 65536;

/// Folds one interleaved chunk into the buckets, starting at absolute frame `first`.
void fold(Peaks &peaks, std::span<const float> interleaved, int channels, std::int64_t first, double framesPerBucket)
{
    const std::int64_t frames = static_cast<std::int64_t>(interleaved.size()) / channels;
    for (std::int64_t f = 0; f < frames; ++f) {
        float peak = 0.0F;
        for (int c = 0; c < channels; ++c) {
            peak = std::max(peak, std::abs(interleaved[static_cast<std::size_t>((f * channels) + c)]));
        }
        const auto bucket = static_cast<std::size_t>(static_cast<double>(first + f) / framesPerBucket);
        if (bucket < peaks.buckets.size()) {
            peaks.buckets[bucket] = std::max(peaks.buckets[bucket], peak);
        }
    }
}

} // namespace

Result<Peaks> computePeaks(const std::filesystem::path &path,
                           std::size_t bucketCount,
                           const std::stop_token &stop,
                           const jobs::Report &report)
{
    AudioFileReader reader;
    if (Result<void> r = reader.open(path); !r) {
        return std::unexpected(r.error());
    }
    const AudioFileInfo &info = reader.info();
    Peaks peaks;
    peaks.sampleRate = info.sampleRate;
    peaks.seconds = info.sampleRate > 0.0 ? static_cast<double>(info.frames) / info.sampleRate : 0.0;
    peaks.buckets.assign(std::max<std::size_t>(1, bucketCount), 0.0F);
    const double framesPerBucket =
        std::max(1.0, static_cast<double>(info.frames) / static_cast<double>(peaks.buckets.size()));
    std::vector<float> chunk(static_cast<std::size_t>(chunkFrames * info.channels));
    std::int64_t position = 0;
    while (true) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, path.string());
        }
        const std::int64_t got = reader.read(chunk);
        if (got <= 0) {
            break;
        }
        fold(peaks,
             std::span(chunk).first(static_cast<std::size_t>(got * info.channels)),
             info.channels,
             position,
             framesPerBucket);
        position += got;
        if (info.frames > 0) {
            report(static_cast<double>(position) / static_cast<double>(info.frames));
        }
    }
    return peaks;
}

} // namespace cc::media
