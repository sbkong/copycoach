// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The prediction cache (copy-spec.md section 16.2 D-3): the three grey layers round-trip through JSON, an
// empty cache removes the file, and a broken file reads as FileCorrupt naming the field.
#include "core/io/jsonfile.h"
#include "core/project/predictioncache.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace cc;
using namespace cc::project;

TEST_CASE("predictioncache: beats, notes and chords round-trip and an empty cache removes the file")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "predictioncache";
    std::filesystem::remove_all(dir);
    const std::filesystem::path path = dir / "cache" / "predictions" / "src-1.json";

    PredictionCache cache;
    cache.beats = predict::BeatPrediction{{0.5, 1.0, 1.5, 2.0}, {0.5, 2.0}, 2.4};
    cache.notes = std::vector<predict::NoteEvent>{{0.1, 0.4, 52, 0.8}, {0.5, 0.9, 55, 0.6}};
    cache.chords = std::vector<predict::ChordSegment>{{0.0, 1.0, 1}, {1.0, 2.4, 6}};
    REQUIRE(savePredictionCache(path, cache));
    REQUIRE(std::filesystem::exists(path));

    const Result<PredictionCache> back = loadPredictionCache(path);
    REQUIRE(back);
    const predict::BeatPrediction beats = back->beats.value_or(predict::BeatPrediction{});
    CHECK(beats.downbeats == std::vector<double>{0.5, 2.0});
    CHECK(beats.seconds == 2.4);
    const std::vector<predict::NoteEvent> notes = back->notes.value_or(std::vector<predict::NoteEvent>{});
    REQUIRE(notes.size() == 2);
    CHECK(notes[1].midi == 55);
    CHECK(notes[1].amplitude == 0.6);
    const std::vector<predict::ChordSegment> chords = back->chords.value_or(std::vector<predict::ChordSegment>{});
    REQUIRE(chords.size() == 2);
    CHECK(chords[1].label == 6);
    CHECK(chords[1].endSeconds == 2.4);

    REQUIRE(savePredictionCache(path, PredictionCache{}));
    CHECK_FALSE(std::filesystem::exists(path));
    const Result<PredictionCache> missing = loadPredictionCache(path);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::FileNotFound);
}

TEST_CASE("predictioncache: a broken file is FileCorrupt and names the field")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "predictioncache";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "broken.json";
    {
        std::ofstream file(path);
        file << R"({"schema_version": 1, "notes": [{"start": 0.1, "end": 0.2, "midi": 400, "amplitude": 0.5}]})";
    }
    const Result<PredictionCache> result = loadPredictionCache(path);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::FileCorrupt);
    CHECK(result.error().subject.contains("midi"));
}
