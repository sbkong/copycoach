// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/beatthis.h"

#include "core/media/loadaudio.h"
#include "core/predict/ortsession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace cc::predict
{

namespace
{

constexpr float maskValue = -1000.0F;
constexpr std::size_t peakHalfKernel = 3; // kernel 7
constexpr double dedupWidthFrames = 1.0;
constexpr double decodeShare = 0.2; // of the progress bar: decoding, then the model

} // namespace

Result<std::vector<float>>
loadMono22k(const std::filesystem::path &audio, const std::stop_token &stop, const jobs::Report &report)
{
    return media::loadResampled(audio, logMelSampleRate, 1, stop, [&report](double p) {
        report(decodeShare * p);
    });
}

namespace
{

/// Window starts of split_piece (beat_this.inference): from -border, stepping chunk - 2 border; the last one
/// is pulled back so it ends at the piece's end.
std::vector<std::ptrdiff_t> windowStarts(std::size_t total)
{
    const auto chunk = static_cast<std::ptrdiff_t>(beatThisChunkFrames);
    const auto border = static_cast<std::ptrdiff_t>(beatThisBorderFrames);
    const auto end = static_cast<std::ptrdiff_t>(total) + border;
    std::vector<std::ptrdiff_t> starts;
    for (std::ptrdiff_t start = -border;; start += chunk - (2 * border)) {
        if (start + chunk >= end) {
            starts.push_back(total > beatThisChunkFrames ? end - chunk : start);
            return starts;
        }
        starts.push_back(start);
    }
}

void fillWindow(const LogMel &frames, std::ptrdiff_t start, std::span<float> window)
{
    std::ranges::fill(window, 0.0F);
    const auto total = static_cast<std::ptrdiff_t>(frames.frames);
    for (std::size_t i = 0; i < beatThisChunkFrames; ++i) {
        const std::ptrdiff_t f = start + static_cast<std::ptrdiff_t>(i);
        if (f >= 0 && f < total) {
            std::copy_n(frames.values.begin() + (f * static_cast<std::ptrdiff_t>(logMelBins)),
                        logMelBins,
                        window.begin() + static_cast<std::ptrdiff_t>(i * logMelBins));
        }
    }
}

/// aggregate_prediction with keep_first: frames inside the border, not yet written, take this window's logits.
void keepFirst(std::ptrdiff_t start,
               std::span<const float> beat,
               std::span<const float> downbeat,
               BeatLogits &logits,
               std::vector<bool> &written)
{
    const auto total = static_cast<std::ptrdiff_t>(written.size());
    for (std::size_t i = beatThisBorderFrames; i + beatThisBorderFrames < beatThisChunkFrames; ++i) {
        const std::ptrdiff_t f = start + static_cast<std::ptrdiff_t>(i);
        if (f < 0 || f >= total || written[static_cast<std::size_t>(f)]) {
            continue;
        }
        logits.beat[static_cast<std::size_t>(f)] = beat[i];
        logits.downbeat[static_cast<std::size_t>(f)] = downbeat[i];
        written[static_cast<std::size_t>(f)] = true;
    }
}

constexpr const char *beatThisInput = "spectrogram";
constexpr std::array<const char *, 2> beatThisOutputs{"beat", "downbeat"};

} // namespace

/// split_piece / aggregate_prediction with keep_first (beat_this.inference): windows start at -border and
/// step by chunk - 2 border; the last one is pulled back to end at the piece; frames already written stay.
Result<BeatLogits> runBeatThis(const LogMel &frames,
                               const std::filesystem::path &model,
                               const std::stop_token &stop,
                               const jobs::Report &report)
{
    OrtSession session;
    if (Result<void> r = session.open(model); !r) {
        return std::unexpected(r.error());
    }
    const std::size_t total = frames.frames;
    BeatLogits logits{std::vector<float>(total, maskValue), std::vector<float>(total, maskValue)};
    std::vector<bool> written(total, false);
    std::vector<float> window(beatThisChunkFrames * logMelBins);
    const std::array<std::int64_t, 3> shape{1,
                                            static_cast<std::int64_t>(beatThisChunkFrames),
                                            static_cast<std::int64_t>(logMelBins)};
    std::vector<std::vector<float>> outputs;
    const std::vector<std::ptrdiff_t> starts = windowStarts(total);
    for (std::size_t w = 0; w < starts.size(); ++w) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, model.string());
        }
        fillWindow(frames, starts[w], window);
        if (Result<void> r = session.run(beatThisInput, shape, window, beatThisOutputs, outputs); !r) {
            return std::unexpected(r.error());
        }
        keepFirst(starts[w], outputs[0], outputs[1], logits, written);
        report(decodeShare + ((1.0 - decodeShare) * static_cast<double>(w + 1) / static_cast<double>(starts.size())));
    }
    return logits;
}

namespace
{

/// Frame indices of local maxima (kernel 7) above the threshold, duplicates within one frame merged.
std::vector<double> peaks(std::span<const float> logits)
{
    std::vector<double> frames;
    for (std::size_t i = 0; i < logits.size(); ++i) {
        if (logits[i] <= 0.0F) {
            continue;
        }
        bool top = true;
        const std::size_t from = i >= peakHalfKernel ? i - peakHalfKernel : 0;
        const std::size_t to = std::min(logits.size() - 1, i + peakHalfKernel);
        for (std::size_t k = from; k <= to && top; ++k) {
            top = logits[k] <= logits[i];
        }
        if (top) {
            frames.push_back(static_cast<double>(i));
        }
    }
    std::vector<double> merged;
    std::size_t count = 0;
    for (const double f : frames) {
        if (!merged.empty() && f - merged.back() <= dedupWidthFrames) {
            ++count;
            merged.back() += (f - merged.back()) / static_cast<double>(count);
        } else {
            merged.push_back(f);
            count = 1;
        }
    }
    return merged;
}

double nearestBeat(double seconds, std::span<const double> beats)
{
    if (beats.empty()) {
        return seconds;
    }
    return *std::ranges::min_element(beats, {}, [seconds](double b) {
        return std::abs(b - seconds);
    });
}

/// Each downbeat moved to the nearest beat, duplicates dropped (the paper's post-processing keeps the beat grid).
std::vector<double> snapToBeats(std::span<const double> downbeats, std::span<const double> beats)
{
    std::vector<double> out;
    for (const double raw : downbeats) {
        const double seconds = nearestBeat(raw, beats);
        if (out.empty() || out.back() != seconds) {
            out.push_back(seconds);
        }
    }
    return out;
}

} // namespace

BeatPrediction minimalPostprocess(const BeatLogits &logits, double fps)
{
    BeatPrediction out;
    for (const double f : peaks(logits.beat)) {
        out.beats.push_back(f / fps);
    }
    std::vector<double> downbeats;
    for (const double f : peaks(logits.downbeat)) {
        downbeats.push_back(f / fps);
    }
    out.downbeats = snapToBeats(downbeats, out.beats);
    out.seconds = static_cast<double>(logits.beat.size()) / fps;
    return out;
}

Result<BeatPrediction> predictBeats(const std::filesystem::path &audio,
                                    const BeatThisFiles &files,
                                    const std::stop_token &stop,
                                    const jobs::Report &report)
{
    LogMelFilterbank filterbank;
    if (Result<void> r = filterbank.load(files.filterbank); !r) {
        return std::unexpected(r.error());
    }
    Result<std::vector<float>> signal = loadMono22k(audio, stop, report);
    if (!signal) {
        return std::unexpected(signal.error());
    }
    const LogMel frames = logMel(*signal, filterbank);
    Result<BeatLogits> logits = runBeatThis(frames, files.model, stop, report);
    if (!logits) {
        return std::unexpected(logits.error());
    }
    BeatPrediction prediction = minimalPostprocess(*logits);
    prediction.seconds = static_cast<double>(signal->size()) / logMelSampleRate;
    return prediction;
}

model::Prediction toPrediction(const BeatPrediction &prediction, const std::string &sourceId)
{
    model::Prediction out;
    out.kind = model::PredictionKind::Beats;
    out.sourceId = sourceId;
    std::size_t bar = 0;
    for (const double seconds : prediction.beats) {
        model::PredictedItem item;
        item.seconds = seconds;
        const bool downbeat = std::ranges::find(prediction.downbeats, seconds) != prediction.downbeats.end();
        item.text = downbeat ? std::to_string(++bar) : std::string("beat");
        item.confidence = 1.0;
        out.items.push_back(std::move(item));
    }
    out.measureTo = bar > 0 ? bar - 1 : 0;
    return out;
}

} // namespace cc::predict
