// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"
#include "core/model/prediction.h"
#include "core/model/timemap.h"
#include "core/predict/cqt.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

// Chord prediction with ChordMini (copy-spec.md section 4 "코드", ptnghia-j/ChordMini, MIT): the log-CQT in
// windows of 108 frames (10 s) through the ChordNet 2E1D student (normalisation in the graph), logits
// smoothed over 9 frames inside each window, argmax per frame, equal neighbours merged into segments. The
// vocabulary is 12 roots x 14 qualities plus X (unknown) and N (no chord), in the upstream order; the app
// spells them as its chord symbol parser reads them ("C:min7" -> "Cm7", "A#:hdim7" -> "Bbm7b5" with flats).
namespace cc::predict
{

constexpr std::size_t chordMiniSeqLen = 108;
constexpr std::size_t chordMiniClasses = 170;
constexpr std::size_t chordMiniSmoothing = 9; // frames, uniform, zero-padded (avg_pool1d)
constexpr int chordMiniQualities = 14;
constexpr int chordMiniUnknown = 168; // "X"
constexpr int chordMiniNoChord = 169; // "N"

struct ChordSegment {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int label = chordMiniNoChord; // vocabulary index
};

/// The model over the frames, one label per frame; Cancelled when the token asks between windows.
[[nodiscard]] Result<std::vector<int>> runChordMini(const LogCqt &features,
                                                    const std::filesystem::path &model,
                                                    const std::stop_token &stop,
                                                    const jobs::Report &report);
/// Runs of equal labels as segments in seconds.
[[nodiscard]] std::vector<ChordSegment> mergeRuns(std::span<const int> labels, double fps = cqtFps);
/// The upstream label ("C:min7", "D#", "X", "N").
[[nodiscard]] std::string chordMiniLabel(int label);
/// The app's spelling of a label ("Cm7"; "Ebm7b5" with flats, "D#m7b5" without); empty for X and N.
[[nodiscard]] std::string chordSymbolText(int label, bool flats);

/// Decode, mix down, resample, estimate the tuning (keycents.h), CQT, the model, merge.
[[nodiscard]] Result<std::vector<ChordSegment>> predictChords(const std::filesystem::path &audio,
                                                              const std::filesystem::path &model,
                                                              const std::stop_token &stop,
                                                              const jobs::Report &report);

/// The prediction layer form: one item per chord change (text = symbol, dur 0) at the start second mapped
/// to ticks and snapped to the nearest quarter; X and N are dropped, repeats of the same symbol merged,
/// and of two changes on one quarter the later wins.
[[nodiscard]] model::Prediction toPrediction(std::span<const ChordSegment> segments,
                                             const std::string &sourceId,
                                             const model::TimeMap &map,
                                             bool flats);

} // namespace cc::predict
