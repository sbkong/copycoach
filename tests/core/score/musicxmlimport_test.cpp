// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// MusicXML import (score-data-path.md section 8, copy-spec.md section 16.2 D-4): the golden files hold our own
// model as JSON (scorejson), so a change in what the importer reads shows up as a diff. Regenerate them with the
// environment variable CC_UPDATE_GOLDEN=1 after checking the new output by hand.
#include "core/project/scorejson.h"
#include "core/score/musicxmlimport.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cc;
using namespace cc::score;
using nlohmann::json;

namespace
{

std::filesystem::path fixtures()
{
    return std::filesystem::path(CC_FIXTURE_DIR) / "score";
}

ScoreImport importFixture(const char *name)
{
    model::IdGenerator ids = model::IdGenerator::seeded(1);
    Result<ScoreImport> r = importMusicXml(fixtures() / name, ids);
    INFO((r ? std::string{} : r.error().detail));
    REQUIRE(r);
    return std::move(*r);
}

/// Compares the imported score with <name>.golden.json; writes it instead when CC_UPDATE_GOLDEN is set.
void requireGolden(const model::Score &score, const char *name)
{
    const std::filesystem::path path = fixtures() / (std::string(name) + ".golden.json");
    const json actual = project::scoreToJson(score);
    if (std::getenv("CC_UPDATE_GOLDEN") != nullptr) { // NOLINT(concurrency-mt-unsafe) test only
        std::ofstream(path) << actual.dump(2) << '\n';
    }
    std::ifstream file(path);
    INFO("golden file: " << path.string());
    REQUIRE(file);
    const json expected = json::parse(file);
    INFO(json::diff(expected, actual).dump(2));
    REQUIRE(actual == expected);
}

const model::Beat &
beatAt(const model::Score &score, std::size_t measure, std::size_t track, int voice, model::Tick tick)
{
    for (const model::Beat &beat : score.measures[measure].beats) {
        if (beat.track == track && beat.voice == voice && beat.tick == tick) {
            return beat;
        }
    }
    FAIL("no beat at measure " << measure << " track " << track << " voice " << voice << " tick " << tick);
}

} // namespace

TEST_CASE("musicxml: the spike's staff + TAB file imports from its TAB staff, meter changes and all")
{
    const ScoreImport in = importFixture("tab-techniques.musicxml");
    const model::Score &score = in.score;
    REQUIRE(score.tracks.size() == 1);
    REQUIRE(score.tracks[0].name == "Guitar");
    REQUIRE(score.tracks[0].tuning.strings == theory::Tuning::standardSix().strings);
    REQUIRE(score.measures.size() == 3);
    REQUIRE(score.initialTimeSig == model::TimeSignature{4, 4});
    REQUIRE(score.measures[1].timeSig == model::TimeSignature{3, 4});
    REQUIRE(score.measures[2].timeSig == model::TimeSignature{4, 4});
    REQUIRE(score.initialTempoBpm == 120.0); // none in the file
    REQUIRE(score.measures[0].beats.size() == 4); // the notation staff's copies are not read twice
    REQUIRE(beatAt(score, 0, 0, 1, 960).notes[0].string == 2);
    REQUIRE(beatAt(score, 0, 0, 1, 960).notes[0].fret == 3);
    REQUIRE(beatAt(score, 1, 0, 1, 1920).rest);
    REQUIRE(in.warnings.size() == 1);
    requireGolden(score, "tab-techniques.musicxml");
}

TEST_CASE("musicxml: tuning with capo, tempo, swing, key and meter changes, sections, chord symbols, words")
{
    const ScoreImport in = importFixture("import-features.musicxml");
    const model::Score &score = in.score;
    REQUIRE(in.title == "Import Features");
    REQUIRE(in.credits == std::vector<std::string>{"Transcribed by hand"});
    REQUIRE(score.tracks.size() == 2);
    REQUIRE(score.tracks[0].tuning.strings == std::vector<theory::Midi>{64, 59, 55, 50, 45, 38}); // drop D
    REQUIRE(score.tracks[0].tuning.capo == 2);
    REQUIRE(score.tracks[1].tuning.strings == theory::Tuning::standardSix().strings); // assumed, warned

    REQUIRE(score.initialTempoBpm == 140.0);
    REQUIRE(score.initialSwingRatio == 2.0 / 3.0);
    REQUIRE(score.measures.size() == 2);
    REQUIRE(score.measures[1].tempoBpm == 100.0);
    REQUIRE(score.measures[1].timeSig == model::TimeSignature{3, 4});
    REQUIRE(score.measures[1].keySig == model::KeySignature{-1, false});
    REQUIRE(score.sections.size() == 2);
    REQUIRE(score.sections[0].letter == "A");
    REQUIRE(score.sections[1].name == "Bridge");
    REQUIRE(score.sections[1].startMeasure == 1);

    REQUIRE(score.measures[0].chords.size() == 2);
    REQUIRE(score.measures[0].chords[0].text == "Dm7");
    REQUIRE(score.measures[0].chords[1].text == "G7b9");
    REQUIRE(score.measures[0].chords[1].tick == 1920);
    REQUIRE(score.measures[1].chords[0].text == "C/E");
    REQUIRE(in.texts.size() == 1);
    REQUIRE(in.texts[0].text == "watch the b9");

    // part 1, voice 5 -> voice 1: dotted quarter, tied eighth, triplet, chord of two notes
    const model::Beat &dotted = beatAt(score, 0, 0, 1, 0);
    REQUIRE(dotted.dur == model::Dur::Quarter);
    REQUIRE(dotted.dots == 1);
    REQUIRE(beatAt(score, 0, 0, 1, 1440).notes[0].tieToNext);
    const model::Beat &triplet = beatAt(score, 0, 0, 1, 2240);
    REQUIRE(triplet.tuplet.has_value());
    REQUIRE(triplet.tuplet.value_or(model::Tuplet{0, 0}).num == 3);
    REQUIRE(beatAt(score, 0, 0, 1, 2880).notes.size() == 2);
    // voice 6 -> voice 2: a half rest then the low D
    REQUIRE(beatAt(score, 0, 0, 2, 0).rest);
    REQUIRE(beatAt(score, 0, 0, 2, 1920).notes[0].string == 6);
    // measure 2: the grace note is gone, forward skipped a beat, the typeless note became a quarter
    REQUIRE(score.measures[1].beats.size() == 3); // guitar: 2, bass: 1
    REQUIRE_FALSE(beatAt(score, 1, 0, 1, 0).notes[0].tieToNext);
    REQUIRE(beatAt(score, 1, 0, 1, 1920).dur == model::Dur::Quarter);

    // part 2 has no TAB staff: positions come from the pitch, the lowest fret wins; a 7th string is recomputed
    REQUIRE(beatAt(score, 0, 1, 1, 0).notes[0].string == 6);
    REQUIRE(beatAt(score, 0, 1, 1, 0).notes[0].fret == 0);
    REQUIRE(beatAt(score, 0, 1, 1, 1920).notes[0].string == 5);
    REQUIRE(beatAt(score, 0, 1, 1, 1920).notes[0].fret == 0);
    REQUIRE(beatAt(score, 1, 1, 1, 0).notes[0].string == 5);
    REQUIRE(beatAt(score, 1, 1, 1, 0).notes[0].fret == 2);
    REQUIRE(in.warnings.size() == 2); // grace note, assumed tuning
    requireGolden(score, "import-features.musicxml");
}

TEST_CASE("musicxml: missing, broken, timewise and compressed files are refused with the right code")
{
    model::IdGenerator ids = model::IdGenerator::seeded(2);
    REQUIRE(importMusicXml(fixtures() / "nope.musicxml", ids).error().code == ErrorCode::FileNotFound);
    REQUIRE(importMusicXml(fixtures() / "song.mxl", ids).error().code == ErrorCode::Unsupported);
    REQUIRE(importMusicXmlData("<score-partwise><part-list/></score-partwise>", "x.musicxml", ids).error().code
            == ErrorCode::FileCorrupt);
    REQUIRE(importMusicXmlData("<score-timewise/>", "x.musicxml", ids).error().code == ErrorCode::Unsupported);
    REQUIRE(importMusicXmlData("<not xml", "x.musicxml", ids).error().code == ErrorCode::FileCorrupt);
}
