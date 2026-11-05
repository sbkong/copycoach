// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/duration.h"
#include "core/theory/pitch.h"
#include "core/theory/tuning.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// What an engine guessed from audio (copy-spec.md section 4): a grey layer beside the score, never inside it.
// Items turn into model notes or chord symbols only through an "accept" command, which also fixes a fingering,
// because model::Note has no pitch of its own (score-data-path.md section 2). Kept under the project's cache/,
// so it can always be regenerated (copy-spec.md section 16.2 D3).
namespace cc::model
{

enum class PredictionKind : std::uint8_t {
    Beats, // bar grid: one item per downbeat, text = bar number
    Key, // one item, text = key name, confidence
    Chords, // one item per chord change, text = symbol
    Notes, // one item per note, midi + optional position
    Fingering, // positions for notes that already have a pitch
};

struct PredictedItem {
    Tick tick = 0; // absolute, from the start of the score
    Tick dur = 0; // 0 when the item is a point (a downbeat, a chord change)
    std::optional<theory::Midi> midi;
    std::optional<theory::StringFret> position;
    std::string text;
    double confidence = 0.0; // 0..1 as the engine reports it; views do not show it yet (copy-spec.md section 4)
    double seconds = 0.0; // Beats: where in the recording (the tick is unknown until accepted)
};

struct Prediction {
    PredictionKind kind = PredictionKind::Notes;
    std::string sourceId; // the audio version it was computed from (copy-spec.md section 1 `sources[]`)
    std::size_t measureFrom = 0;
    std::size_t measureTo = 0; // inclusive
    std::vector<PredictedItem> items; // ascending by tick
};

} // namespace cc::model
