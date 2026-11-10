// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/chordmini.h"

#include "core/predict/beatthis.h"
#include "core/predict/keycents.h"
#include "core/predict/ortsession.h"
#include "core/theory/constants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace cc::predict
{

namespace
{

constexpr double decodeShare = 0.2; // of the progress bar: decoding, then the CQT, then the model
constexpr double cqtShare = 0.5;
constexpr const char *chordMiniInput = "features";
constexpr std::array<const char *, 1> chordMiniOutputs{"logits"};

// Upstream order (src/utils/chords.py idx2voca_chord) and the app's spelling of each quality.
constexpr std::array<std::string_view, chordMiniQualities> upstreamQualities{"min",
                                                                             "maj",
                                                                             "dim",
                                                                             "aug",
                                                                             "min6",
                                                                             "maj6",
                                                                             "min7",
                                                                             "minmaj7",
                                                                             "maj7",
                                                                             "7",
                                                                             "dim7",
                                                                             "hdim7",
                                                                             "sus2",
                                                                             "sus4"};
constexpr std::array<std::string_view, chordMiniQualities>
    appQualities{"m", "", "dim", "aug", "m6", "6", "m7", "mMaj7", "maj7", "7", "dim7", "m7b5", "sus2", "sus4"};
constexpr std::array<std::string_view, 12> sharpRoots{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<std::string_view, 12> flatRoots{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

/// Zero-padded features of the window starting at `first`, frames past the end stay zero.
void fillWindow(const LogCqt &features, std::size_t first, std::span<float> window)
{
    std::ranges::fill(window, 0.0F);
    const std::size_t count = std::min(chordMiniSeqLen, features.frames - first);
    std::copy_n(features.values.begin() + static_cast<std::ptrdiff_t>(first * cqtBins),
                count * cqtBins,
                window.begin());
}

/// avg_pool1d(kernel 9, stride 1, padding 4) over the window's frames, then argmax: the frame's label.
int smoothedArgmax(std::span<const float> logits, std::size_t frame)
{
    constexpr std::size_t half = chordMiniSmoothing / 2;
    const std::size_t from = frame >= half ? frame - half : 0;
    const std::size_t to = std::min(chordMiniSeqLen - 1, frame + half);
    int best = 0;
    float bestSum = -INFINITY;
    for (std::size_t c = 0; c < chordMiniClasses; ++c) {
        float sum = 0.0F;
        for (std::size_t f = from; f <= to; ++f) {
            sum += logits[(f * chordMiniClasses) + c];
        }
        if (sum > bestSum) {
            bestSum = sum;
            best = static_cast<int>(c);
        }
    }
    return best;
}

} // namespace

Result<std::vector<int>> runChordMini(const LogCqt &features,
                                      const std::filesystem::path &model,
                                      const std::stop_token &stop,
                                      const jobs::Report &report)
{
    OrtSession session;
    if (Result<void> r = session.open(model); !r) {
        return std::unexpected(r.error());
    }
    std::vector<int> labels(features.frames, chordMiniNoChord);
    std::vector<float> window(chordMiniSeqLen * cqtBins);
    const std::array<std::int64_t, 3> shape{1,
                                            static_cast<std::int64_t>(chordMiniSeqLen),
                                            static_cast<std::int64_t>(cqtBins)};
    std::vector<std::vector<float>> outputs;
    const std::size_t windows = (features.frames + chordMiniSeqLen - 1) / chordMiniSeqLen;
    for (std::size_t w = 0; w < windows; ++w) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, model.string());
        }
        const std::size_t first = w * chordMiniSeqLen;
        fillWindow(features, first, window);
        if (Result<void> r = session.run(chordMiniInput, shape, window, chordMiniOutputs, outputs); !r) {
            return std::unexpected(r.error());
        }
        for (std::size_t f = 0; f < chordMiniSeqLen && first + f < features.frames; ++f) {
            labels[first + f] = smoothedArgmax(outputs[0], f);
        }
        report(static_cast<double>(w + 1) / static_cast<double>(windows));
    }
    return labels;
}

std::vector<ChordSegment> mergeRuns(std::span<const int> labels, double fps)
{
    std::vector<ChordSegment> segments;
    for (std::size_t f = 0; f < labels.size(); ++f) {
        const double end = static_cast<double>(f + 1) / fps;
        if (!segments.empty() && segments.back().label == labels[f]) {
            segments.back().endSeconds = end;
        } else {
            segments.push_back({static_cast<double>(f) / fps, end, labels[f]});
        }
    }
    return segments;
}

std::string chordMiniLabel(int label)
{
    if (label == chordMiniUnknown) {
        return "X";
    }
    if (label == chordMiniNoChord || label < 0) {
        return "N";
    }
    const std::string root(sharpRoots[static_cast<std::size_t>(label / chordMiniQualities)]);
    const std::string_view quality = upstreamQualities[static_cast<std::size_t>(label % chordMiniQualities)];
    return quality == "maj" ? root : root + ":" + std::string(quality);
}

std::string chordSymbolText(int label, bool flats)
{
    if (label < 0 || label >= chordMiniUnknown) {
        return {};
    }
    const auto root = static_cast<std::size_t>(label / chordMiniQualities);
    const auto quality = static_cast<std::size_t>(label % chordMiniQualities);
    return std::string(flats ? flatRoots[root] : sharpRoots[root]) + std::string(appQualities[quality]);
}

Result<std::vector<ChordSegment>> predictChords(const std::filesystem::path &audio,
                                                const std::filesystem::path &model,
                                                const std::stop_token &stop,
                                                const jobs::Report &report)
{
    Result<std::vector<float>> signal = loadMono22k(audio, stop, report);
    if (!signal) {
        return std::unexpected(signal.error());
    }
    const double cents = estimateKeyCents(*signal).cents;
    Result<LogCqt> features = logCqt(*signal, cents, stop, [&report](double p) {
        report(decodeShare + (cqtShare * p));
    });
    if (!features) {
        return std::unexpected(features.error());
    }
    Result<std::vector<int>> labels = runChordMini(*features, model, stop, [&report](double p) {
        report(decodeShare + cqtShare + ((1.0 - decodeShare - cqtShare) * p));
    });
    if (!labels) {
        return std::unexpected(labels.error());
    }
    return mergeRuns(*labels);
}

model::Prediction
toPrediction(std::span<const ChordSegment> segments, const std::string &sourceId, const model::TimeMap &map, bool flats)
{
    model::Prediction out;
    out.kind = model::PredictionKind::Chords;
    out.sourceId = sourceId;
    constexpr model::Tick quarter = theory::ticksPerQuarter;
    for (const ChordSegment &segment : segments) {
        std::string text = chordSymbolText(segment.label, flats);
        if (text.empty()) {
            continue;
        }
        const model::Tick tick =
            std::llround(static_cast<double>(map.secondsToTick(segment.startSeconds)) / quarter) * quarter;
        if (!out.items.empty() && out.items.back().tick == tick) {
            out.items.pop_back(); // the later change on the same quarter wins
        }
        if (!out.items.empty() && out.items.back().text == text) {
            continue;
        }
        model::PredictedItem item;
        item.tick = tick;
        item.text = std::move(text);
        item.confidence = 1.0;
        item.seconds = segment.startSeconds;
        out.items.push_back(std::move(item));
    }
    return out;
}

} // namespace cc::predict
