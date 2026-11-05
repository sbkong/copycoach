// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"
#include "core/model/prediction.h"
#include "core/predict/logmel.h"

#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

// Beat and downbeat tracking with Beat This! through ONNX Runtime (copy-spec.md section 4): decode the
// recording, 22050 Hz mono, log-mel frames, the model over overlapping 1500-frame windows (border 6, first
// window wins), then the paper's "minimal" post-processing. Runs as a job: cancel through the token,
// progress through the callback. Without ONNX Runtime in the build it reports Unsupported.
namespace cc::predict
{

constexpr std::size_t beatThisChunkFrames = 1500;
constexpr std::size_t beatThisBorderFrames = 6;

struct BeatThisFiles {
    std::filesystem::path model; // beat_this-*.onnx
    std::filesystem::path filterbank; // mel-filterbank.bin
};

struct BeatPrediction {
    std::vector<double> beats; // seconds, ascending
    std::vector<double> downbeats; // seconds, each one of the beats
    double seconds = 0.0; // length of the recording
};

/// Frame-wise logits of the model over the whole piece.
struct BeatLogits {
    std::vector<float> beat;
    std::vector<float> downbeat;
};

/// The recording as mono 22050 Hz samples (any input rate or channel count).
Result<std::vector<float>>
loadMono22k(const std::filesystem::path &audio, const std::stop_token &stop, const jobs::Report &report);
/// The model over the frames; Cancelled when the token asks between windows.
Result<BeatLogits> runBeatThis(const LogMel &frames,
                               const std::filesystem::path &model,
                               const std::stop_token &stop,
                               const jobs::Report &report);
/// Minimal post-processing: local maxima (kernel 7) above logit 0, duplicates within a frame merged, downbeats snapped
/// to beats.
BeatPrediction minimalPostprocess(const BeatLogits &logits, double fps = logMelFps);

Result<BeatPrediction> predictBeats(const std::filesystem::path &audio,
                                    const BeatThisFiles &files,
                                    const std::stop_token &stop,
                                    const jobs::Report &report);

/// The prediction layer form: one item per downbeat (text = bar number) and the beats as items with text "beat".
model::Prediction toPrediction(const BeatPrediction &prediction, const std::string &sourceId);

} // namespace cc::predict
