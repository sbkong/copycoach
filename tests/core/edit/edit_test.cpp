// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/commands.h"
#include "core/edit/undostack.h"
#include "core/model/scorebuilder.h"
#include "core/project/projectfile.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace cc;
using namespace cc::edit;
using nlohmann::json;

namespace
{

constexpr model::Tick bar = 3840; // one 4/4 measure

/// Four 4/4 measures of quarter notes on track 0 plus a bass track, sections, a range with a memo, sync points.
project::Project sampleProject(model::IdGenerator &ids)
{
    model::ScoreSpec spec;
    spec.tempoBpm = 120.0;
    spec.measures.assign(4, model::MeasureSpec{{{model::Dur::Quarter, {{1, 3}}}, {model::Dur::Quarter, {{2, 5}}}}});
    project::Project p = project::newProject();
    p.score = model::buildScore(spec, ids);
    p.score.tracks.push_back(model::Track{ids.next(), "Bass", theory::Tuning::standardSix(), model::TrackRole::Bass});
    p.score.measures[2].tempoBpm = 96.0;
    p.score.measures[2].timeSig = model::TimeSignature{3, 4};
    p.score.sections = {{ids.next(), 0, "A", "Head"}, {ids.next(), 2, "B", "Bridge"}};
    p.sources = {{ids.next(), project::SourceKind::Audio, "media/take1.flac", "", 0, "Studio"}};
    p.syncPoints = {{p.sources[0].id, {0, 1.0}}, {p.sources[0].id, {bar * 2, 5.0}}, {p.sources[0].id, {bar * 3, 7.0}}};
    p.ranges = {{ids.next(), "ii-V", 1, 2, 0, 80, 0}, {ids.next(), "ending", 3, 3, 1, 100, 0}};
    p.memos.ranges[p.ranges[0].id] = "slow";
    p.phrases = {{ids.next(), p.ranges[1].id, theory::Tuning::standardSix(), {}, "", {}, {}}};
    p.lastSession.rangeId = p.ranges[1].id;
    p.lastSession.playheadTick = (bar * 3) + 960;
    return p;
}

json snapshot(const project::Project &p)
{
    return project::projectToJson(p);
}

const model::Beat *beatAt(const model::Score &score, const BeatRef &at)
{
    for (const model::Beat &beat : score.measures[at.measure].beats) {
        if (beat.track == at.track && beat.voice == at.voice && beat.tick == at.tick) {
            return &beat;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("edit: writing notes creates, extends and clears beats, in order, with undo and redo by snapshot")
{
    model::IdGenerator ids = model::IdGenerator::seeded(7);
    project::Project p = sampleProject(ids);
    const json before = snapshot(p);
    UndoStack stack;

    // a new beat between the two quarters, on the second voice: inserted in (track, voice, tick) order
    const BeatRef at{0, 0, 2, 480};
    REQUIRE(stack.push(p, setNote(p.score, at, {3, 5}, model::Dur::Eighth, ids)) == DirtyRange::measures(0, 0));
    REQUIRE(p.score.measures[0].beats.size() == 3);
    REQUIRE(p.score.measures[0].beats.back().voice == 2);
    REQUIRE(beatAt(p.score, at)->notes[0].fret == 5);
    REQUIRE(beatAt(p.score, at)->dur == model::Dur::Eighth);

    // a second string on the same beat makes a chord; the same fret again is a no-op
    stack.push(p, setNote(p.score, at, {4, 7}, model::Dur::Quarter, ids));
    REQUIRE(beatAt(p.score, at)->notes.size() == 2);
    REQUIRE(setNote(p.score, at, {4, 7}, model::Dur::Quarter, ids) == nullptr);
    REQUIRE(setNote(p.score, at, {7, 0}, model::Dur::Quarter, ids) == nullptr); // no 7th string
    REQUIRE(setNote(p.score, at, {1, maxFret + 1}, model::Dur::Quarter, ids) == nullptr);

    // removing both notes leaves a rest, then the rest can go
    stack.push(p, removeNote(p.score, at, 3));
    stack.push(p, removeNote(p.score, at, 4));
    REQUIRE(beatAt(p.score, at)->rest);
    REQUIRE(beatAt(p.score, at)->notes.empty());
    stack.push(p, deleteBeat(p.score, at));
    REQUIRE(beatAt(p.score, at) == nullptr);
    REQUIRE(stack.modified());

    while (stack.canUndo()) {
        stack.undo(p);
    }
    REQUIRE(snapshot(p) == before);
    REQUIRE_FALSE(stack.modified());
    REQUIRE(stack.redo(p) == DirtyRange::measures(0, 0));
    REQUIRE(beatAt(p.score, at)->notes[0].fret == 5);
    REQUIRE(stack.modified());
}

TEST_CASE("edit: beat length, ties, rests and measure changes validate their input and report timing")
{
    model::IdGenerator ids = model::IdGenerator::seeded(8);
    project::Project p = sampleProject(ids);
    UndoStack stack;
    const BeatRef first{1, 0, 1, 0};

    REQUIRE(setBeatLength(p.score, first, model::Dur::Quarter, 0, std::nullopt) == nullptr); // unchanged
    REQUIRE(setBeatLength(p.score, first, model::Dur::Quarter, 3, std::nullopt) == nullptr); // three dots
    stack.push(p, setBeatLength(p.score, first, model::Dur::Eighth, 1, model::Tuplet{3, 2}));
    REQUIRE(beatAt(p.score, first)->ticks() == 480);
    REQUIRE(setTie(p.score, first, 2, true) == nullptr); // no note on string 2
    stack.push(p, setTie(p.score, first, 1, true));
    REQUIRE(beatAt(p.score, first)->notes[0].tieToNext);
    stack.push(p, setRest(p.score, first, model::Dur::Half, ids));
    REQUIRE(beatAt(p.score, first)->rest);
    REQUIRE(setRest(p.score, first, model::Dur::Half, ids) == nullptr);

    REQUIRE(setTempo(p.score, 0, 0.0) == nullptr);
    REQUIRE(setTempo(p.score, 2, 96.0) == nullptr); // already 96
    REQUIRE(stack.push(p, setTempo(p.score, 0, 140.0)) == DirtyRange::measures(0, 0, true));
    REQUIRE(p.score.tempoAt(1) == 140.0);
    REQUIRE(stack.push(p, setTempo(p.score, 2, std::nullopt)) == DirtyRange::measures(2, 2, true));
    REQUIRE(p.score.tempoAt(3) == 140.0);
    REQUIRE(setTimeSig(p.score, 0, model::TimeSignature{4, 3}) == nullptr);
    REQUIRE(stack.push(p, setTimeSig(p.score, 0, model::TimeSignature{6, 8})).timing);
    REQUIRE(stack.push(p, setKeySig(p.score, 1, model::KeySignature{-2, false})) == DirtyRange::measures(1, 1));
    REQUIRE(setKeySig(p.score, 1, model::KeySignature{8, false}) == nullptr);
    REQUIRE(setSwing(p.score, 1, 0.9) == nullptr);
    stack.push(p, setSwing(p.score, 1, 2.0 / 3.0));
    REQUIRE(p.score.swingAt(3) == 2.0 / 3.0);

    stack.push(p, setChordSymbol(p.score, 0, 1920, "G7", ids));
    stack.push(p, setChordSymbol(p.score, 0, 0, "Dm7", ids));
    REQUIRE(p.score.measures[0].chords[0].text == "Dm7"); // kept in tick order
    REQUIRE(p.score.measures[0].chords[1].text == "G7");
    REQUIRE(setChordSymbol(p.score, 0, 0, "Dm7", ids) == nullptr);
    stack.push(p, setChordSymbol(p.score, 0, 0, "", ids));
    REQUIRE(p.score.measures[0].chords.size() == 1);
    REQUIRE(setChordSymbol(p.score, 0, 0, "", ids) == nullptr);

    const json edited = snapshot(p);
    while (stack.canUndo()) {
        stack.undo(p);
    }
    while (stack.canRedo()) {
        stack.redo(p);
    }
    REQUIRE(snapshot(p) == edited);
}

TEST_CASE("edit: inserting measures shifts sections, ranges, sync points and the playhead; undo restores all")
{
    model::IdGenerator ids = model::IdGenerator::seeded(9);
    project::Project p = sampleProject(ids);
    const json before = snapshot(p);
    UndoStack stack;

    REQUIRE(insertMeasures(p, 1, 0, ids) == nullptr);
    REQUIRE(stack.push(p, insertMeasures(p, 1, 2, ids)) == DirtyRange::all(true));
    REQUIRE(p.score.measures.size() == 6);
    REQUIRE(p.score.measures[1].beats.empty());
    REQUIRE(model::isWellFormedId(p.score.measures[2].id));
    REQUIRE(p.score.measures[4].tempoBpm == 96.0); // the old measure 2 moved with its changes
    REQUIRE(p.score.sections[0].startMeasure == 0);
    REQUIRE(p.score.sections[1].startMeasure == 4);
    REQUIRE(p.ranges[0].barFrom == 3);
    REQUIRE(p.ranges[0].barTo == 4);
    REQUIRE(p.ranges[1].barFrom == 5);
    REQUIRE(p.syncPoints[0].point.tick == 0);
    REQUIRE(p.syncPoints[1].point.tick == bar * 4); // two 4/4 measures later
    REQUIRE(p.lastSession.playheadTick == (bar * 5) + 960);

    stack.undo(p);
    REQUIRE(snapshot(p) == before);
    stack.redo(p);
    REQUIRE(p.score.measures.size() == 6);
    stack.undo(p);
    REQUIRE(snapshot(p) == before);
}

TEST_CASE("edit: deleting measures clips or drops ranges, drops sync points inside, carries changes forward")
{
    model::IdGenerator ids = model::IdGenerator::seeded(10);
    project::Project p = sampleProject(ids);
    const json before = snapshot(p);
    UndoStack stack;

    // delete measures 2..3: section B and range "ending" vanish, "ii-V" is clipped to bar 1, the phrase and the
    // last session forget the range, the sync points at 2 and 3 go, the playhead moves to the new end
    stack.push(p, deleteMeasures(p, 2, 2));
    REQUIRE(p.score.measures.size() == 2);
    REQUIRE(p.score.sections.size() == 1);
    REQUIRE(p.ranges.size() == 1);
    REQUIRE(p.ranges[0].barTo == 1);
    REQUIRE_FALSE(p.phrases[0].rangeId);
    REQUIRE_FALSE(p.lastSession.rangeId);
    REQUIRE(p.memos.ranges.size() == 1);
    REQUIRE(p.syncPoints.size() == 1);
    REQUIRE(p.lastSession.playheadTick == bar * 2);
    stack.undo(p);
    REQUIRE(snapshot(p) == before);

    // delete measure 2 only: measure 3 inherits the 3/4 meter and 96 bpm that measure 2 introduced
    stack.push(p, deleteMeasures(p, 2, 1));
    REQUIRE(p.score.measures.size() == 3);
    REQUIRE(p.score.measures[2].timeSig == model::TimeSignature{3, 4});
    REQUIRE(p.score.measures[2].tempoBpm == 96.0);
    REQUIRE(p.score.measures[2].beats.size() == 2);
    REQUIRE(p.ranges[1].barFrom == 2);
    REQUIRE(p.syncPoints.size() == 2);
    REQUIRE(p.syncPoints[1].point.tick == (bar * 3) - 2880); // one 3/4 measure earlier
    stack.undo(p);
    REQUIRE(snapshot(p) == before);
}

TEST_CASE("edit: sections, tracks and project fields are value edits with no-op detection")
{
    model::IdGenerator ids = model::IdGenerator::seeded(11);
    project::Project p = sampleProject(ids);
    const json before = snapshot(p);
    UndoStack stack;

    REQUIRE(setSection(p.score, 1, "", "", ids) == nullptr);
    REQUIRE(setSection(p.score, 0, "A", "Head", ids) == nullptr);
    stack.push(p, setSection(p.score, 1, "A2", "", ids));
    REQUIRE(p.score.sections.size() == 3);
    REQUIRE(p.score.sections[1].startMeasure == 1);
    stack.push(p, setSection(p.score, 0, "", "", ids));
    REQUIRE(p.score.sections[0].letter == "A2");

    model::Track bass = p.score.tracks[1];
    REQUIRE(setTrack(p.score, 1, bass) == nullptr);
    bass.tuning.capo = 2;
    bass.id = "ignored1";
    REQUIRE(stack.push(p, setTrack(p.score, 1, bass)) == DirtyRange::all());
    REQUIRE(p.score.tracks[1].tuning.capo == 2);
    REQUIRE(p.score.tracks[1].id != "ignored1");

    REQUIRE(setProjectMemo(p, "") == nullptr);
    stack.push(p, setProjectMemo(p, "count slowly"));
    REQUIRE(setRangeMemo(p, "nosuchid", "x") == nullptr);
    REQUIRE(setRangeMemo(p, p.ranges[0].id, "slow") == nullptr);
    stack.push(p, setRangeMemo(p, p.ranges[0].id, ""));
    REQUIRE(p.memos.ranges.empty());
    stack.push(p, setLyrics(p, "la"));

    project::Range bad = p.ranges[0];
    bad.barTo = 99;
    REQUIRE(setRange(p, bad) == nullptr);
    project::Range moved = p.ranges[0];
    moved.speedPct = 60;
    stack.push(p, setRange(p, moved));
    REQUIRE(p.ranges[0].speedPct == 60);
    stack.push(p, setRange(p, project::Range{ids.next(), "new", 0, 0, 0, 100, 0}));
    REQUIRE(p.ranges.size() == 3);
    REQUIRE(removeRange(p, "nosuchid") == nullptr);
    REQUIRE(stack.push(p, removeRange(p, p.ranges[1].id)) == DirtyRange::none());
    REQUIRE(p.ranges.size() == 2);
    REQUIRE_FALSE(p.phrases[0].rangeId);
    REQUIRE_FALSE(p.lastSession.rangeId);

    REQUIRE(setSyncPoints(p, "nosuchid", {}) == nullptr);
    REQUIRE(setSyncPoints(p, p.sources[0].id, {{0, 2.0}, {960, 1.0}}) == nullptr); // not ascending
    REQUIRE(stack.push(p, setSyncPoints(p, p.sources[0].id, {{0, 0.5}})) == DirtyRange::timingOnly());
    REQUIRE(p.syncPoints.size() == 1);
    REQUIRE(setReference(p, {0.0, 200}) == nullptr);
    stack.push(p, setReference(p, {-10.0, 3}));
    REQUIRE(p.reference.tuningNote == 3);

    while (stack.canUndo()) {
        stack.undo(p);
    }
    REQUIRE(snapshot(p) == before);
}

TEST_CASE("edit: accepting a prediction writes notes and chord symbols inside the chosen measures only")
{
    model::IdGenerator ids = model::IdGenerator::seeded(12);
    project::Project p = sampleProject(ids);
    UndoStack stack;

    model::Prediction notes;
    notes.kind = model::PredictionKind::Notes;
    notes.items = {{bar + 1920, 480, 67, theory::StringFret{1, 3}, "", 0.9}, // measure 1, beat 3: eighth
                   {bar + 2880, 1000, 64, theory::StringFret{2, 5}, "", 0.9}, // measure 1: nearest = quarter
                   {(bar * 2) + 960, 480, 60, std::nullopt, "", 0.9}, // no position: skipped
                   {bar * 3, 480, 60, theory::StringFret{1, 0}, "", 0.9}}; // outside the window
    REQUIRE(acceptPrediction(p, notes, {2, 2}, 0, ids) == nullptr); // only the positionless item is there
    REQUIRE(stack.push(p, acceptPrediction(p, notes, {1, 2}, 1, ids)) == DirtyRange::measures(1, 2));
    const model::Beat *eighth = beatAt(p.score, {1, 1, 1, 1920});
    REQUIRE(eighth != nullptr);
    REQUIRE(eighth->dur == model::Dur::Eighth);
    REQUIRE(eighth->notes[0].string == 1);
    REQUIRE(beatAt(p.score, {1, 1, 1, 2880})->dur == model::Dur::Quarter);
    REQUIRE(p.score.measures[3].beats.size() == 2); // untouched

    model::Prediction chords;
    chords.kind = model::PredictionKind::Chords;
    chords.items = {{0, 0, std::nullopt, std::nullopt, "Dm7", 0.5},
                    {1920, 0, std::nullopt, std::nullopt, "G7", 0.5},
                    {bar, 0, std::nullopt, std::nullopt, "Cmaj7", 0.5}};
    stack.push(p, acceptPrediction(p, chords, {0, 0}, 0, ids));
    REQUIRE(p.score.measures[0].chords.size() == 2);
    REQUIRE(p.score.measures[1].chords.empty());

    model::Prediction key;
    key.kind = model::PredictionKind::Key;
    key.items = {{0, 0, std::nullopt, std::nullopt, "F", 0.5}};
    REQUIRE(acceptPrediction(p, key, {0, 0}, 0, ids) == nullptr);
}

TEST_CASE("edit: the undo stack drops the redo branch on push, bounds its depth and tracks the saved state")
{
    model::IdGenerator ids = model::IdGenerator::seeded(13);
    project::Project p = sampleProject(ids);
    UndoStack stack(3);

    REQUIRE(stack.push(p, nullptr) == DirtyRange::none());
    REQUIRE_FALSE(stack.modified());
    REQUIRE(stack.undo(p) == DirtyRange::none());
    for (int i = 0; i < 3; ++i) {
        stack.push(p, setProjectMemo(p, "memo " + std::to_string(i)));
    }
    stack.markSaved();
    REQUIRE_FALSE(stack.modified());
    stack.undo(p);
    REQUIRE(stack.modified());
    REQUIRE(p.memos.project == "memo 1");
    stack.push(p, setProjectMemo(p, "branch"));
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(stack.modified()); // the saved state was on the dropped branch
    stack.redo(p);
    REQUIRE(p.memos.project == "branch");

    stack.push(p, setProjectMemo(p, "four")); // depth 3: the oldest command falls off
    REQUIRE(stack.canUndo());
    stack.undo(p);
    stack.undo(p);
    stack.undo(p);
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE(p.memos.project == "memo 0");
    stack.clear();
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE_FALSE(stack.modified());
}
