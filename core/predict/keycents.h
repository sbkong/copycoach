// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"
#include "core/model/prediction.h"
#include "core/model/score.h"
#include "core/theory/pitch.h"

#include <array>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>

// Key and tuning reference of a recording, own DSP (copy-spec.md section 4 "키 · 센트"): spectral peaks give
// the A4 offset in cents (how far the recording sits from 440 Hz), a pitch-class chroma corrected by that
// offset is matched against the Krumhansl-Kessler key profiles (Krumhansl, Cognitive Foundations of Musical
// Pitch, 1990, table 2.1) for the key. Suggestions for the stage-1 reference values, never applied by itself.
namespace cc::predict
{

constexpr int keyCentsSampleRate = 22050; // the same mono front end as the beat tracker
constexpr std::size_t keyCentsFftSize = 8192; // 2.7 Hz bins: fine enough for cents around low notes
constexpr std::size_t keyCentsHop = 4096;

struct KeyCentsPrediction {
    theory::PitchClass tonic = 0;
    bool minor = false;
    double keyConfidence = 0.0; // correlation with the best profile, 0..1
    double keyMargin = 0.0; // best minus second-best correlation: how clear the choice is
    double cents = 0.0; // A4 offset, -50..50
    double centsConfidence = 0.0; // share of peak energy within 10 cents of the estimate, 0..1
    std::array<double, 12> chroma{}; // normalised, index = pitch class
};

/// The whole analysis on mono 22050 Hz samples (no file access; tests feed synthesised signals).
[[nodiscard]] KeyCentsPrediction estimateKeyCents(std::span<const float> mono22k);
/// Decode, mix down, resample, analyse; Cancelled between frames.
[[nodiscard]] Result<KeyCentsPrediction>
predictKeyCents(const std::filesystem::path &audio, const std::stop_token &stop, const jobs::Report &report);

/// "F major" / "D minor" with flat spellings.
[[nodiscard]] std::string keyName(const KeyCentsPrediction &prediction);
/// The key signature the predicted key is written in (relative major for minor keys).
[[nodiscard]] model::KeySignature keySignatureOf(const KeyCentsPrediction &prediction);
/// The prediction layer form: one Key item (text = key name, confidence = correlation).
[[nodiscard]] model::Prediction toPrediction(const KeyCentsPrediction &prediction, const std::string &sourceId);

} // namespace cc::predict
