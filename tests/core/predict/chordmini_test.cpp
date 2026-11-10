// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Chord prediction (copy-spec.md section 4 "코드"): the own CQT against librosa's on the export fixture, the
// vocabulary spelled as the chord symbol parser reads it, runs merged and snapped to quarters, and, when the
// exported model is present, the fixture's E major chord recognised as the Python side did.
#include "core/model/scorebuilder.h"
#include "core/model/timemap.h"
#include "core/predict/chordmini.h"
#include "core/predict/chordminimodel.h"
#include "core/theory/chordsymbol.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::predict;

namespace
{

const std::filesystem::path s_model = std::filesystem::path(CC_CHORDMINI_DIR) / chordMiniModel.name;
const std::filesystem::path s_fixtureDir = std::filesystem::path(CC_FIXTURE_DIR) / "predict";

std::vector<float> readFloats(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    file.seekg(0, std::ios::end);
    const auto bytes = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    file.read(reinterpret_cast<char *>(out.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
              static_cast<std::streamsize>(bytes));
    return out;
}

std::string majorityLabel(const std::vector<std::string> &labels)
{
    std::map<std::string, int> counts;
    for (const std::string &label : labels) {
        ++counts[label];
    }
    std::string best;
    int bestCount = -1;
    for (const auto &[label, count] : counts) {
        if (count > bestCount) {
            best = label;
            bestCount = count;
        }
    }
    return best;
}

} // namespace

TEST_CASE("cqt: the own transform matches librosa where the signal sounds")
{
    const std::vector<float> signal = readFloats(s_fixtureDir / "cqt-signal.f32");
    const std::vector<float> reference = readFloats(s_fixtureDir / "cqt-reference.f32");
    const std::stop_token never;
    const Result<LogCqt> ours = logCqt(signal, 0.0, never, [](double) { });
    REQUIRE(ours);
    REQUIRE(ours->frames == reference.size() / cqtBins); // 33 frames of 3 s
    constexpr float sounding = -4.0F; // log magnitude of a bin with a partial in it
    double soundingSum = 0.0;
    double soundingMax = 0.0;
    double allSum = 0.0;
    std::size_t soundingCount = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const double diff = std::abs(ours->values[i] - reference[i]);
        allSum += diff;
        if (reference[i] > sounding) {
            soundingSum += diff;
            soundingMax = std::max(soundingMax, diff);
            ++soundingCount;
        }
    }
    REQUIRE(soundingCount > 1000);
    CHECK(soundingSum / static_cast<double>(soundingCount) < 0.15); // measured 0.06 (export script)
    CHECK(soundingMax < 2.0); // measured 1.25
    CHECK(allSum / static_cast<double>(reference.size()) < 0.3); // measured 0.14 (quiet valleys differ)
}

TEST_CASE("chordmini: every chord in the vocabulary is spelled so the chord symbol parser reads it")
{
    for (int label = 0; label < chordMiniUnknown; ++label) {
        for (const bool flats : {false, true}) {
            const std::string text = chordSymbolText(label, flats);
            INFO(chordMiniLabel(label) << " -> " << text);
            const std::optional<theory::ParsedChordSymbol> parsed = theory::parseChordSymbol(text);
            REQUIRE(parsed.has_value());
            CHECK(parsed.value_or(theory::ParsedChordSymbol{}).root == label / chordMiniQualities);
        }
    }
    CHECK(chordMiniLabel(0) == "C:min");
    CHECK(chordMiniLabel(1) == "C");
    CHECK(chordMiniLabel(168) == "X");
    CHECK(chordMiniLabel(169) == "N");
    CHECK(chordSymbolText(6, false) == "Cm7");
    CHECK(chordSymbolText((10 * chordMiniQualities) + 11, true) == "Bbm7b5"); // A#:hdim7 with flats
    CHECK(chordSymbolText((1 * chordMiniQualities) + 8, false) == "C#maj7");
    CHECK(chordSymbolText((3 * chordMiniQualities) + 7, true) == "EbmMaj7");
    CHECK(chordSymbolText(chordMiniUnknown, false).empty());
    CHECK(chordSymbolText(chordMiniNoChord, false).empty());
}

TEST_CASE("chordmini: frame labels merge into segments and snap to quarters as chord symbols")
{
    const std::vector<int> labels{169, 169, 1, 1, 1, 1, 6, 6, 6, 168, 1, 1};
    const std::vector<ChordSegment> segments = mergeRuns(labels, 10.0);
    REQUIRE(segments.size() == 5);
    CHECK(segments[1].label == 1);
    CHECK(segments[1].startSeconds == 0.2);
    CHECK(segments[1].endSeconds == 0.6);
    CHECK(segments[4].startSeconds == 1.0);

    model::IdGenerator ids = model::IdGenerator::seeded(5);
    model::ScoreSpec spec;
    spec.tempoBpm = 120.0; // a quarter = 0.5 s = 960 ticks
    spec.measures.assign(2, model::MeasureSpec{{{model::Dur::Whole, {{1, 3}}}}});
    const model::Score score = model::buildScore(spec, ids);
    const model::TimeMap map = model::TimeMap::fromSyncPoints(score, {}, 1000.0);
    const model::Prediction prediction = toPrediction(segments, "src", map, false);
    CHECK(prediction.kind == model::PredictionKind::Chords);
    REQUIRE(prediction.items.size() == 3); // N and X dropped
    CHECK(prediction.items[0].text == "C");
    CHECK(prediction.items[0].tick == 0); // 0.2 s rounds to quarter 0
    CHECK(prediction.items[1].text == "Cm7");
    CHECK(prediction.items[1].tick == 960); // 0.6 s rounds to quarter 1
    CHECK(prediction.items[2].text == "C");
    CHECK(prediction.items[2].tick == 1920); // 1.0 s: a change back is a change

    // two changes on one quarter: the later wins; a repeat of the same symbol is not a change
    const std::vector<ChordSegment> crowded{{0.0, 0.1, 1}, {0.1, 0.5, 6}, {0.5, 1.0, 6}, {1.0, 1.5, 1}};
    const model::Prediction later = toPrediction(crowded, "src", map, false);
    REQUIRE(later.items.size() == 2);
    CHECK(later.items[0].text == "Cm7");
    CHECK(later.items[1].text == "C");
    CHECK(later.items[1].tick == 1920);
}

TEST_CASE("chordmini: the fixture's E major chord comes back as the Python side labelled it")
{
    if (!std::filesystem::exists(s_model)) {
        SKIP("chordmini-2e1d.onnx not exported (tools/models/export_chordmini.py)");
    }
    const std::vector<float> signal = readFloats(s_fixtureDir / "cqt-signal.f32");
    const std::stop_token never;
    const Result<LogCqt> features = logCqt(signal, 0.0, never, [](double) { });
    REQUIRE(features);
    std::vector<double> progress;
    const Result<std::vector<int>> labels = runChordMini(*features, s_model, never, [&progress](double p) {
        progress.push_back(p);
    });
    REQUIRE(labels);
    REQUIRE(labels->size() == features->frames);
    REQUIRE(progress.back() == 1.0);
    std::vector<std::string> ours;
    for (const int label : *labels) {
        ours.push_back(chordMiniLabel(label));
    }
    std::vector<std::string> expected;
    std::ifstream file(s_fixtureDir / "chordmini-labels.txt");
    for (std::string line; std::getline(file, line);) {
        expected.push_back(line);
    }
    REQUIRE(expected.size() == ours.size());
    CHECK(majorityLabel(ours) == majorityLabel(expected));
    CHECK(majorityLabel(ours) == "E");
}
