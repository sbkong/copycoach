// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// MusicXML export (score-data-path.md section 8): the import fixture goes model -> MusicXML -> model without
// losing what the model holds (tunings, meter / key / tempo / swing changes, sections, chord symbols, voices,
// ties, dots, a tuplet, strings and frets), and a built score exports with its harmony spelled back.
#include "core/model/scorebuilder.h"
#include "core/score/musicxmlexport.h"
#include "core/score/musicxmlimport.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <string>

using namespace cc;
using namespace cc::score;

namespace
{

const std::filesystem::path s_fixture = std::filesystem::path(CC_FIXTURE_DIR) / "score" / "import-features.musicxml";

void requireSameMeasure(const model::Measure &a, const model::Measure &b)
{
    CHECK(a.timeSig == b.timeSig);
    CHECK(a.keySig == b.keySig);
    CHECK(a.tempoBpm == b.tempoBpm);
    CHECK(std::abs(a.swingRatio.value_or(0.0) - b.swingRatio.value_or(0.0)) < 1e-3);
    REQUIRE(a.chords.size() == b.chords.size());
    for (std::size_t c = 0; c < a.chords.size(); ++c) {
        CHECK(a.chords[c].tick == b.chords[c].tick);
        CHECK(a.chords[c].text == b.chords[c].text);
    }
    REQUIRE(a.beats.size() == b.beats.size());
    for (std::size_t i = 0; i < a.beats.size(); ++i) {
        const model::Beat &x = a.beats[i];
        const model::Beat &y = b.beats[i];
        INFO("beat " << i << " track " << x.track << " tick " << x.tick);
        CHECK(x.track == y.track);
        CHECK(x.voice == y.voice);
        CHECK(x.tick == y.tick);
        CHECK(x.dur == y.dur);
        CHECK(x.dots == y.dots);
        CHECK(x.tuplet.has_value() == y.tuplet.has_value());
        if (x.tuplet && y.tuplet) {
            CHECK(x.tuplet->num == y.tuplet->num);
            CHECK(x.tuplet->numbase == y.tuplet->numbase);
        }
        CHECK(x.rest == y.rest);
        REQUIRE(x.notes.size() == y.notes.size());
        for (std::size_t n = 0; n < x.notes.size(); ++n) {
            CHECK(x.notes[n].string == y.notes[n].string);
            CHECK(x.notes[n].fret == y.notes[n].fret);
            CHECK(x.notes[n].tieToNext == y.notes[n].tieToNext);
        }
    }
}

} // namespace

TEST_CASE("musicxml export: the import fixture round-trips through the exporter")
{
    model::IdGenerator ids = model::IdGenerator::seeded(3);
    const Result<ScoreImport> first = importMusicXml(s_fixture, ids);
    REQUIRE(first);
    const std::string xml = exportMusicXml(first->score, first->title);
    CHECK(xml.contains("<score-partwise version=\"4.0\">"));
    CHECK(xml.contains("<divisions>960</divisions>"));
    const Result<ScoreImport> second = importMusicXmlData(xml, "exported.musicxml", ids);
    REQUIRE(second);
    const model::Score &a = first->score;
    const model::Score &b = second->score;
    CHECK(second->title == first->title);
    REQUIRE(a.tracks.size() == b.tracks.size());
    for (std::size_t t = 0; t < a.tracks.size(); ++t) {
        CHECK(a.tracks[t].name == b.tracks[t].name);
        CHECK(a.tracks[t].tuning.strings == b.tracks[t].tuning.strings);
        CHECK(a.tracks[t].tuning.capo == b.tracks[t].tuning.capo);
    }
    CHECK(a.initialTimeSig == b.initialTimeSig);
    CHECK(a.initialKeySig == b.initialKeySig);
    CHECK(a.initialTempoBpm == b.initialTempoBpm);
    CHECK(std::abs(a.initialSwingRatio - b.initialSwingRatio) < 1e-3);
    REQUIRE(a.sections.size() == b.sections.size());
    for (std::size_t s = 0; s < a.sections.size(); ++s) {
        CHECK(a.sections[s].startMeasure == b.sections[s].startMeasure);
        CHECK(a.sections[s].letter == b.sections[s].letter);
        CHECK(a.sections[s].name == b.sections[s].name);
    }
    REQUIRE(a.measures.size() == b.measures.size());
    for (std::size_t m = 0; m < a.measures.size(); ++m) {
        INFO("measure " << m);
        requireSameMeasure(a.measures[m], b.measures[m]);
    }
}

TEST_CASE("musicxml export: chord symbols keep their spelling and a file lands on disk")
{
    model::IdGenerator ids = model::IdGenerator::seeded(4);
    model::ScoreSpec spec;
    spec.measures.assign(1, model::MeasureSpec{{{model::Dur::Half, {{1, 3}}}, {model::Dur::Half, {{2, 5}, {3, 4}}}}});
    model::Score score = model::buildScore(spec, ids);
    score.measures[0].chords = {{ids.next(), 0, "Bbm7b5"}, {ids.next(), 1920, "F#7/A#"}, {ids.next(), 2880, "weird"}};
    const std::string xml = exportMusicXml(score, "Chords");
    CHECK(xml.contains("<root-step>B</root-step>"));
    CHECK(xml.contains("<kind text=\"m7b5\">other</kind>"));
    CHECK(xml.contains("<bass-step>A</bass-step>"));
    const Result<ScoreImport> back = importMusicXmlData(xml, "chords.musicxml", ids);
    REQUIRE(back);
    REQUIRE(back->score.measures[0].chords.size() == 3);
    CHECK(back->score.measures[0].chords[0].text == "Bbm7b5");
    CHECK(back->score.measures[0].chords[1].text == "F#7/A#");
    CHECK(back->score.measures[0].chords[2].text == "Cweird"); // unknown text rides on a C root

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "musicxml-export";
    std::filesystem::create_directories(dir);
    REQUIRE(writeMusicXml(dir / "out.musicxml", score, "Chords"));
    CHECK(std::filesystem::file_size(dir / "out.musicxml") > 500);
}
