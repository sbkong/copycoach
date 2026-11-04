// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// project.json (copy-spec.md section 1, piece B-3): UUID shape, a full round trip through disk, the autosave
// rule (no .bak, cleared by an explicit save), and broken files that come back as values, never crashes.
#include "core/io/limits.h"
#include "core/model/scorebuilder.h"
#include "core/project/project.h"
#include "core/project/projectfile.h"
#include "core/project/uuid.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::project;

namespace
{

std::filesystem::path freshDir(const char *name)
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "project" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void writeText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

/// Everything populated once: two tracks, a tempo / key / meter change, chords, a tuplet, a tie, two sections,
/// two sources with their own sync points, a range with a memo, a phrase, a session.
Project sampleProject()
{
    model::IdGenerator ids = model::IdGenerator::seeded(5);
    model::ScoreSpec spec;
    spec.tempoBpm = 120.0;
    spec.measures.assign(4,
                         model::MeasureSpec{{{model::Dur::Quarter, {{1, 3}}, true}, {model::Dur::Quarter, {{1, 3}}}}});
    Project p;
    p.projectId = newUuidV4();
    p.score = model::buildScore(spec, ids);
    p.score.tracks.push_back(model::Track{ids.next(), "Bass", theory::Tuning::standardSix(), model::TrackRole::Bass});
    p.score.tracks.front().role = model::TrackRole::Solo;
    p.score.initialKeySig = {-1, false};
    p.score.measures[2].tempoBpm = 96.0;
    p.score.measures[2].keySig = model::KeySignature{3, true};
    p.score.measures[2].timeSig = model::TimeSignature{3, 4};
    p.score.measures[2].swingRatio = 2.0 / 3.0;
    p.score.measures[0].chords = {{ids.next(), 0, "Dm7"}, {ids.next(), 1920, "G7b9"}};
    model::Beat triplet;
    triplet.id = ids.next();
    triplet.track = 1;
    triplet.tick = 0;
    triplet.dur = model::Dur::Eighth;
    triplet.tuplet = model::Tuplet{3, 2};
    triplet.notes = {{ids.next(), 6, 0, false}};
    p.score.measures[1].beats.push_back(triplet);
    p.score.sections = {{ids.next(), 0, "A", "Head"}, {ids.next(), 2, "B", "Bridge"}};
    p.sources = {{ids.next(), SourceKind::Audio, "media/take1.flac", "abc123", 0, "Studio"},
                 {ids.next(), SourceKind::Video, "media/live.mp4", "", 1, "Live"}};
    p.reference = {-12.5, 3}; // a quarter tone flat, tuned down to Eb
    p.syncPoints = {{p.sources[0].id, {0, 1.0}}, {p.sources[0].id, {3840, 3.0}}, {p.sources[1].id, {0, 0.5}}};
    p.ranges = {{ids.next(), "ii-V", 0, 1, 2, 60, -1}};
    model::Beat line = triplet; // a phrase is its own one-track score
    line.track = 0;
    p.phrases = {{ids.next(),
                  p.ranges[0].id,
                  theory::Tuning::standardSix(),
                  {line},
                  "Dm7 G7",
                  theory::FingeringPattern::Diagonal,
                  {0, 5, 7}}};
    p.memos.project = "Count it off slowly.\n둘째 줄";
    p.memos.ranges[p.ranges[0].id] = "watch the b9";
    p.lyrics = "la la";
    p.stage = 4;
    p.lastSession = {p.ranges[0].id, 1920, 75, p.sources[0].id, nowIso8601Utc()};
    return p;
}

} // namespace

TEST_CASE("project: a v4 UUID has the right shape and two of them differ")
{
    const std::string a = newUuidV4();
    const std::string b = newUuidV4();
    REQUIRE(isUuid(a));
    REQUIRE(a.size() == 36);
    REQUIRE(a[14] == '4');
    REQUIRE((a[19] == '8' || a[19] == '9' || a[19] == 'a' || a[19] == 'b'));
    REQUIRE(a != b);
    REQUIRE_FALSE(isUuid("not-a-uuid"));
    REQUIRE_FALSE(isUuid("123e4567-e89b-12d3-a456-42661417400g"));
}

TEST_CASE("project: new project has an id, stage 1 and one guitar track")
{
    const Project p = newProject();
    REQUIRE(isUuid(p.projectId));
    REQUIRE(p.stage == stageMin);
    REQUIRE(p.score.tracks.size() == 1);
    REQUIRE(p.score.measures.empty());
    REQUIRE(model::isWellFormedId(p.score.tracks[0].id));
}

TEST_CASE("project: save then load reproduces every field, and the second save keeps one .bak")
{
    const auto dir = freshDir("roundtrip");
    const Project original = sampleProject();
    REQUIRE(saveProject(dir, original));
    REQUIRE(std::filesystem::exists(dir / projectFileName));
    REQUIRE_FALSE(std::filesystem::exists(dir / "project.json.bak"));

    const auto loaded = loadProject(dir);
    INFO((loaded ? std::string{} : loaded.error().detail));
    REQUIRE(loaded.has_value());
    REQUIRE(projectToJson(*loaded) == projectToJson(original));
    REQUIRE(loaded->projectId == original.projectId);
    REQUIRE(loaded->score.tracks[1].role == model::TrackRole::Bass);
    REQUIRE(loaded->score.measures[2].tempoBpm == 96.0);
    REQUIRE(loaded->score.measures[2].timeSig == model::TimeSignature{3, 4});
    REQUIRE(loaded->score.measures[1].beats.back().tuplet.value_or(model::Tuplet{0, 0}).num == 3);
    REQUIRE(loaded->score.measures[0].beats[0].notes[0].tieToNext);
    REQUIRE(loaded->score.measures[0].chords[1].text == "G7b9");
    REQUIRE(loaded->score.sections[1].name == "Bridge");
    REQUIRE(loaded->reference.tuningNote == 3);
    REQUIRE(syncPointsFor(*loaded, original.sources[0].id).size() == 2);
    REQUIRE(syncPointsFor(*loaded, original.sources[1].id)[0].seconds == 0.5);
    REQUIRE(loaded->phrases[0].fingeringPattern == theory::FingeringPattern::Diagonal);
    REQUIRE(loaded->phrases[0].keysDone == std::vector<theory::PitchClass>{0, 5, 7});
    REQUIRE(loaded->memos.ranges.at(original.ranges[0].id) == "watch the b9");
    REQUIRE(loaded->lastSession.playheadTick == 1920);
    REQUIRE(loaded->lastSession.savedAt.ends_with('Z'));

    REQUIRE(saveProject(dir, *loaded));
    REQUIRE(std::filesystem::exists(dir / "project.json.bak"));
}

TEST_CASE("project: the autosave rotates no backup and an explicit save removes it")
{
    const auto dir = freshDir("autosave");
    const Project p = sampleProject();
    REQUIRE_FALSE(autosaveIsNewer(dir));
    REQUIRE(saveAutosave(dir, p));
    REQUIRE(saveAutosave(dir, p));
    REQUIRE(std::filesystem::exists(dir / autosaveFileName));
    REQUIRE_FALSE(std::filesystem::exists(dir / "project.autosave.json.bak"));
    REQUIRE(autosaveIsNewer(dir)); // no project.json yet: recovery is the only copy
    const auto recovered = loadAutosave(dir);
    INFO((recovered ? std::string{} : recovered.error().detail));
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->stage == 4);

    REQUIRE(saveProject(dir, p));
    REQUIRE_FALSE(std::filesystem::exists(dir / autosaveFileName));
    REQUIRE_FALSE(autosaveIsNewer(dir));
}

TEST_CASE("project: broken files come back as errors naming the file, dangling references are dropped")
{
    const auto dir = freshDir("broken");
    REQUIRE(loadProject(dir).error().code == ErrorCode::FileNotFound);

    nlohmann::json doc = projectToJson(sampleProject());
    const auto path = dir / projectFileName;

    SECTION("newer schema")
    {
        doc["schema_version"] = 99;
        REQUIRE(projectFromJson(doc, path).error().code == ErrorCode::SchemaTooNew);
    }
    SECTION("string out of the tuning")
    {
        doc["score"]["measures"][0]["beats"][0]["notes"][0]["string"] = 9;
        const auto r = projectFromJson(doc, path);
        REQUIRE(r.error().code == ErrorCode::FileCorrupt);
        REQUIRE(r.error().subject == path.string());
    }
    SECTION("sync point of an unknown source")
    {
        doc["sync_points"][0]["source_id"] = "zzzzzzzz";
        REQUIRE(projectFromJson(doc, path).error().code == ErrorCode::FileCorrupt);
    }
    SECTION("stage, speed and tempo ranges")
    {
        doc["stage"] = 9;
        REQUIRE(projectFromJson(doc, path).error().code == ErrorCode::FileCorrupt);
        doc["stage"] = 2;
        doc["ranges"][0]["speed_pct"] = 10;
        REQUIRE(projectFromJson(doc, path).error().code == ErrorCode::FileCorrupt);
        doc["ranges"][0]["speed_pct"] = 150;
        doc["score"]["measures"][2]["tempo_bpm"] = 0;
        REQUIRE(projectFromJson(doc, path).error().code == ErrorCode::FileCorrupt);
    }
    SECTION("too many tracks")
    {
        for (std::size_t i = 0; i <= io::maxTracks; ++i) {
            doc["score"]["tracks"].push_back(doc["score"]["tracks"][0]);
        }
        REQUIRE(projectFromJson(doc, path).error().code == ErrorCode::FileCorrupt);
    }
    SECTION("unknown fields are ignored, a deleted range takes its memo and references with it")
    {
        doc["future_field"] = {{"x", 1}};
        doc["score"]["tracks"][0]["colour"] = "red";
        doc["ranges"] = nlohmann::json::array();
        const auto r = projectFromJson(doc, path);
        INFO((r ? std::string{} : r.error().detail));
        REQUIRE(r.has_value());
        REQUIRE(r->memos.ranges.empty());
        REQUIRE_FALSE(r->phrases[0].rangeId.has_value());
        REQUIRE_FALSE(r->lastSession.rangeId.has_value());
        REQUIRE(r->lastSession.sourceId.has_value());
    }
    SECTION("not JSON at all")
    {
        writeText(path, "{ this is not json");
        REQUIRE(loadProject(dir).error().code == ErrorCode::FileCorrupt);
    }
}
