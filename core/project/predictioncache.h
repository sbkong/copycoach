// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/predict/basicpitch.h"
#include "core/predict/beatthis.h"
#include "core/predict/chordmini.h"

#include <filesystem>
#include <optional>
#include <vector>

// The grey layers of one audio version, kept under the project's cache/predictions/<source id>.json so
// they survive a reopen (copy-spec.md section 16.2 D-3). Regenerable: a missing or unreadable file only
// means "predict again". The key prediction is not kept: it is an inline offer, accepted or dismissed on
// the spot, and its own DSP recomputes in a second.
namespace cc::project
{

constexpr int predictionCacheVersion = 1;

struct PredictionCache {
    std::optional<predict::BeatPrediction> beats;
    std::optional<std::vector<predict::NoteEvent>> notes;
    std::optional<std::vector<predict::ChordSegment>> chords;

    [[nodiscard]] bool empty() const
    {
        return !beats && !notes && !chords;
    }
};

/// FileNotFound when there is no file, FileCorrupt (with the field in `subject`) when it does not parse.
[[nodiscard]] Result<PredictionCache> loadPredictionCache(const std::filesystem::path &path);
/// Writes atomically; an empty cache removes the file.
[[nodiscard]] Result<void> savePredictionCache(const std::filesystem::path &path, const PredictionCache &cache);

} // namespace cc::project
