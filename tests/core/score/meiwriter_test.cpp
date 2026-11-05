// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// MEI generation (score-data-path.md section 4) checked two ways: a golden MEI text for the import-features
// model (CC_UPDATE_GOLDEN=1 regenerates it) and Verovio itself loading what we write, with our xml:ids found
// again on the page. The spike (f) questions that need no new model fields are answered here too: 7 courses,
// key change scoreDef, tab tuplet + beam, harm, layout time of 100 measures.
#include "core/model/scorebuilder.h"
#include "core/score/meicontext.h"
#include "core/score/meiwriter.h"
#include "core/score/musicxmlimport.h"

#include <catch2/catch_test_macros.hpp>

#include <vrv/toolkit.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace cc;
using namespace cc::score;

namespace
{

std::filesystem::path fixtures()
{
    return std::filesystem::path(CC_FIXTURE_DIR) / "score";
}

model::Score importedFeatures()
{
    model::IdGenerator ids = model::IdGenerator::seeded(1);
    Result<ScoreImport> r = importMusicXml(fixtures() / "import-features.musicxml", ids);
    REQUIRE(r);
    return std::move(r->score);
}

/// Loads the MEI into Verovio; fails the test when Verovio refuses it.
vrv::Toolkit &loaded(vrv::Toolkit &toolkit, const std::string &mei)
{
    REQUIRE(toolkit.SetResourcePath(CC_VEROVIO_DATA_DIR));
    toolkit.SetOptions(
        R"({"breaks": "auto", "pageWidth": 2100, "adjustPageHeight": true, "footer": "none", "header": "none"})");
    REQUIRE(toolkit.LoadData(mei));
    REQUIRE(toolkit.GetPageCount() >= 1);
    return toolkit;
}

model::Score quarters(std::size_t measures, model::IdGenerator &ids)
{
    model::ScoreSpec spec;
    spec.measures.assign(measures,
                         model::MeasureSpec{{{model::Dur::Quarter, {{1, 3}}},
                                             {model::Dur::Quarter, {{2, 5}}},
                                             {model::Dur::Quarter, {{3, 5}}},
                                             {model::Dur::Quarter, {{4, 5}}}}});
    return model::buildScore(spec, ids);
}

} // namespace

TEST_CASE("mei: the import-features model matches its golden MEI and is deterministic")
{
    const model::Score score = importedFeatures();
    const std::string mei = writeMei(score);
    REQUIRE(mei == writeMei(score));
    const std::filesystem::path path = fixtures() / "import-features.musicxml.golden.mei";
    if (std::getenv("CC_UPDATE_GOLDEN") != nullptr) { // NOLINT(concurrency-mt-unsafe) test only
        std::ofstream(path, std::ios::binary) << mei;
    }
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    std::stringstream expected;
    expected << file.rdbuf();
    REQUIRE(mei == expected.str());
}

TEST_CASE("mei: Verovio loads what we write and finds our ids, sections, chord symbols and ties")
{
    const model::Score score = importedFeatures();
    const std::string mei = writeMei(score);
    vrv::Toolkit toolkit(false);
    loaded(toolkit, mei);
    const model::Beat &first = score.measures[0].beats.front();
    REQUIRE(toolkit.GetPageWithElement("m-" + score.measures[0].id) == 1);
    REQUIRE(toolkit.GetPageWithElement("b-" + first.id) == 1);
    REQUIRE(toolkit.GetPageWithElement("ns-" + first.notes[0].id) == 1);
    REQUIRE(toolkit.GetPageWithElement("n-" + first.notes[0].id) == 1);
    REQUIRE(toolkit.GetPageWithElement("h-" + score.measures[0].chords[0].id) == 1);
    REQUIRE(toolkit.GetPageWithElement("sd-" + score.sections[0].id) == 1);
    REQUIRE(mei.contains("<tie startid=\"#ns-"));
    REQUIRE(mei.contains("<tuplet num=\"3\" numbase=\"2\">"));
    REQUIRE(mei.contains("<harm xml:id=\"h-"));
    REQUIRE(mei.contains("label=\"A\""));
    REQUIRE(mei.contains("label=\"Bridge\""));
    REQUIRE(mei.contains("<meterSig count=\"3\" unit=\"4\" />")); // the change before measure 2
    REQUIRE(mei.contains("<keySig sig=\"1f\" />"));
    REQUIRE(mei.contains("lines=\"6\""));
    REQUIRE(mei.contains("mm=\"100\""));
}

TEST_CASE("mei: tab-only writes one staff per track and Verovio still lays it out")
{
    const model::Score score = importedFeatures();
    const MeiOptions options{true};
    const std::string mei = writeMei(score, options);
    REQUIRE(tabStaffOf(0, options) == 1);
    REQUIRE(tabStaffOf(1, options) == 2);
    REQUIRE_FALSE(mei.contains("shape=\"G\""));
    REQUIRE_FALSE(mei.contains("<grpSym"));
    REQUIRE(mei.contains("<staffDef n=\"2\" notationtype=\"tab.guitar\""));
    vrv::Toolkit toolkit(false);
    loaded(toolkit, mei);
}

TEST_CASE("mei: spelling follows the key signature and marks the key's own accidentals as gestural")
{
    using detail::spell;
    const model::KeySignature c{0, false};
    const model::KeySignature g{1, false};
    const model::KeySignature f{-1, false};
    REQUIRE(spell(64, c).pname == 'e');
    REQUIRE(spell(64, c).accid.empty());
    REQUIRE(spell(64, c).octave == 4);
    REQUIRE(spell(61, c).pname == 'd'); // C sharp spelled as D flat in C
    REQUIRE(spell(61, c).accid == "f");
    REQUIRE_FALSE(spell(61, c).inKey);
    REQUIRE(spell(66, g).pname == 'f');
    REQUIRE(spell(66, g).accid == "s");
    REQUIRE(spell(66, g).inKey);
    REQUIRE(spell(65, g).accid == "n"); // F natural against the key's F sharp
    REQUIRE(spell(70, f).pname == 'b');
    REQUIRE(spell(70, f).inKey);
    REQUIRE(spell(71, f).accid == "n");
}

TEST_CASE("mei: spike (f) items that need no new model fields, answered by loading into Verovio")
{
    model::IdGenerator ids = model::IdGenerator::seeded(3);
    SECTION("seven courses")
    {
        model::Score score = quarters(1, ids);
        score.tracks[0].tuning.strings.push_back(35); // low B
        const std::string mei = writeMei(score);
        REQUIRE(mei.contains("lines=\"7\""));
        REQUIRE(mei.contains("<course n=\"7\""));
        vrv::Toolkit toolkit(false);
        loaded(toolkit, mei);
    }
    SECTION("key change in the middle, tab tuplet with beams")
    {
        model::Score score = quarters(3, ids);
        score.measures[1].keySig = model::KeySignature{3, false};
        for (model::Beat &beat : score.measures[2].beats) {
            beat.dur = model::Dur::Eighth;
            beat.tuplet = model::Tuplet{3, 2};
        }
        score.measures[2].beats[1].tick = 320;
        score.measures[2].beats[2].tick = 640;
        score.measures[2].beats[3].tick = 960;
        const std::string mei = writeMei(score);
        REQUIRE(mei.contains("<keySig sig=\"3s\" />"));
        REQUIRE(mei.contains(
            "<tuplet num=\"3\" numbase=\"2\">\n                    <beam>\n                      <tabGrp"));
        vrv::Toolkit toolkit(false);
        loaded(toolkit, mei);
        REQUIRE(toolkit.GetPageWithElement("b-" + score.measures[2].beats[1].id) == 1);
    }
    SECTION("one hundred measures lay out in bounded time")
    {
        const model::Score score = quarters(100, ids);
        const std::string mei = writeMei(score);
        vrv::Toolkit toolkit(false);
        const auto start = std::chrono::steady_clock::now();
        loaded(toolkit, mei);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        INFO("100 measures: " << elapsed.count() << " ms, pages " << toolkit.GetPageCount());
        REQUIRE(elapsed < std::chrono::seconds(20)); // debug build; the number itself goes to the doc
        REQUIRE(toolkit.GetPageWithElement("m-" + score.measures[99].id) >= 1);
    }
}
