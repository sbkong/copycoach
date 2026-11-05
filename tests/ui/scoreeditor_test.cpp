// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Keyboard editing (editor.* actions through ScoreEditor): the cursor walks beats, measures, strings and
// tracks; digits write frets through the undo stack (two digits = one fret, one command); rest, length, dot,
// tie and delete edit the beat under the cursor; undo and redo walk back and forth.
#include "core/model/scorebuilder.h"
#include "ui/score/scoreeditor.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

using namespace cc;
using namespace cc::ui;

namespace
{

/// Two 4/4 measures: quarters on track 0, a half note then nothing on track 1.
model::Score twoTracks()
{
    model::IdGenerator ids = model::IdGenerator::seeded(21);
    model::ScoreSpec spec;
    const model::MeasureSpec quarters{{{model::Dur::Quarter, {{1, 3}}},
                                       {model::Dur::Quarter, {{2, 5}}},
                                       {model::Dur::Quarter, {{3, 5}}},
                                       {model::Dur::Quarter, {{4, 5}}}}};
    spec.measures = {quarters, quarters};
    model::Score score = model::buildScore(spec, ids);
    score.tracks.push_back(model::Track{ids.next(), "Bass", theory::Tuning::standardSix(), model::TrackRole::Bass});
    model::Beat half;
    half.id = ids.next();
    half.track = 1;
    half.tick = 0;
    half.dur = model::Dur::Half;
    half.notes = {{ids.next(), 6, 0, false}};
    score.measures[0].beats.push_back(half);
    return score;
}

BeatLocation cursorOf(const ScoreEditor &editor)
{
    const auto cursor = editor.cursor();
    REQUIRE(cursor.has_value());
    return cursor.value_or(BeatLocation{});
}

const model::Beat &beatAt(const model::Score &score, const BeatLocation &at)
{
    for (const model::Beat &beat : score.measures[at.measure].beats) {
        if (beat.track == at.track && beat.voice == at.voice && beat.tick == at.tick) {
            return beat;
        }
    }
    FAIL("no beat at measure " << at.measure << " tick " << at.tick);
}

} // namespace

TEST_CASE("scoreeditor: the cursor walks beats, slots, measures, strings and tracks")
{
    ScoreEditor editor;
    editor.setScore(twoTracks());
    REQUIRE(cursorOf(editor) == BeatLocation{0, 0, 1, 0});
    REQUIRE(editor.cursorString() == 1);

    editor.move(ScoreEditor::Move::Right);
    REQUIRE(cursorOf(editor).tick == 960);
    editor.move(ScoreEditor::Move::BarEnd);
    REQUIRE(cursorOf(editor).tick == 2880);
    editor.move(ScoreEditor::Move::Right); // the last quarter fills the measure: on to measure 2
    REQUIRE(cursorOf(editor) == BeatLocation{1, 0, 1, 0});
    editor.move(ScoreEditor::Move::Left);
    REQUIRE(cursorOf(editor) == BeatLocation{0, 0, 1, 2880});
    editor.move(ScoreEditor::Move::LastBar);
    REQUIRE(cursorOf(editor).measure == 1);
    editor.move(ScoreEditor::Move::BarEnd);
    editor.move(ScoreEditor::Move::Right); // end of the score: stays
    REQUIRE(cursorOf(editor) == BeatLocation{1, 0, 1, 2880});
    editor.move(ScoreEditor::Move::FirstBar);
    REQUIRE(cursorOf(editor) == BeatLocation{0, 0, 1, 0});

    editor.move(ScoreEditor::Move::Down);
    editor.move(ScoreEditor::Move::Down);
    REQUIRE(editor.cursorString() == 3);
    for (int i = 0; i < 10; ++i) {
        editor.move(ScoreEditor::Move::Down);
    }
    REQUIRE(editor.cursorString() == 6); // clamped to the tuning
    editor.move(ScoreEditor::Move::Up);
    REQUIRE(editor.cursorString() == 5);

    editor.move(ScoreEditor::Move::Right);
    editor.move(ScoreEditor::Move::TrackNext); // bass has only the half note at 0: snaps back to it
    REQUIRE(cursorOf(editor) == BeatLocation{0, 1, 1, 0});
    editor.move(ScoreEditor::Move::Right); // the half note ends at 1920 with room left: an empty slot
    REQUIRE(cursorOf(editor) == BeatLocation{0, 1, 1, 1920});
    editor.move(ScoreEditor::Move::TrackPrev);
    REQUIRE(cursorOf(editor) == BeatLocation{0, 0, 1, 1920});
}

TEST_CASE("scoreeditor: digits write a fret on the cursor string, two digits make one fret and one command")
{
    ScoreEditor editor;
    editor.setScore(twoTracks());
    const QSignalSpy changed(&editor, &ScoreEditor::scoreChanged);
    editor.move(ScoreEditor::Move::Down); // string 2
    editor.typeDigit(1);
    REQUIRE(editor.pendingDigits() == "1");
    REQUIRE(beatAt(editor.score(), cursorOf(editor)).notes.size() == 2); // string 1 fret 3 and string 2 fret 1
    editor.typeDigit(2);
    REQUIRE(editor.pendingDigits() == "12");
    const model::Beat &beat = beatAt(editor.score(), cursorOf(editor));
    REQUIRE(beat.notes.size() == 2);
    REQUIRE(beat.notes[1].string == 2);
    REQUIRE(beat.notes[1].fret == 12);
    REQUIRE(editor.undoStack().canUndo());
    editor.undo();
    REQUIRE(beatAt(editor.score(), cursorOf(editor)).notes.size() == 1); // one undo removes the whole fret
    REQUIRE_FALSE(editor.undoStack().canUndo());
    editor.redo();
    REQUIRE(beatAt(editor.score(), cursorOf(editor)).notes[1].fret == 12);
    REQUIRE(changed.count() == 4); // two digits = 2, undo, redo

    editor.confirm(); // steps right, digits start over
    REQUIRE(editor.pendingDigits().empty());
    REQUIRE(cursorOf(editor).tick == 960);
    editor.typeDigit(9);
    editor.typeDigit(9); // 99 is past the fret limit: starts over with 9
    REQUIRE(editor.pendingDigits() == "9");
    REQUIRE(beatAt(editor.score(), cursorOf(editor)).notes[0].fret == 9); // string 2 had fret 5: changed in place
}

TEST_CASE("scoreeditor: a note typed into an empty slot opens a beat of the input length")
{
    ScoreEditor editor;
    editor.setScore(twoTracks());
    editor.move(ScoreEditor::Move::TrackNext);
    editor.move(ScoreEditor::Move::Right); // the empty slot at 1920 on the bass
    REQUIRE(editor.inputDur() == model::Dur::Quarter);
    editor.shorter();
    REQUIRE(editor.inputDur() == model::Dur::Eighth);
    editor.move(ScoreEditor::Move::Down);
    editor.move(ScoreEditor::Move::Down);
    editor.move(ScoreEditor::Move::Down);
    editor.move(ScoreEditor::Move::Down);
    editor.move(ScoreEditor::Move::Down); // string 6
    editor.typeDigit(5);
    const model::Beat &fresh = beatAt(editor.score(), BeatLocation{0, 1, 1, 1920});
    REQUIRE(fresh.dur == model::Dur::Eighth);
    REQUIRE(fresh.notes[0].string == 6);
    REQUIRE(fresh.notes[0].fret == 5);
    editor.confirm();
    REQUIRE(cursorOf(editor).tick == 1920 + 480); // the slot after the new eighth
}

TEST_CASE("scoreeditor: rest, dot, length, tie and delete act on the beat under the cursor, with undo")
{
    ScoreEditor editor;
    editor.setScore(twoTracks());
    const BeatLocation first = cursorOf(editor);

    editor.toggleDot();
    REQUIRE(beatAt(editor.score(), first).dots == 1);
    editor.toggleDot();
    REQUIRE(beatAt(editor.score(), first).dots == 0);
    editor.longer();
    REQUIRE(beatAt(editor.score(), first).dur == model::Dur::Half);
    editor.toggleTie();
    REQUIRE(beatAt(editor.score(), first).notes[0].tieToNext);
    editor.deleteNote(); // the only note on string 1: the beat becomes a rest
    REQUIRE(beatAt(editor.score(), first).rest);
    editor.toggleRest(); // a rest toggled off is deleted
    REQUIRE(editor.score().measures[0].beats.size() == 4); // 3 quarters + the bass half note
    editor.toggleRest(); // and back: a rest of the input length
    REQUIRE(beatAt(editor.score(), first).rest);
    editor.deleteBeat();
    REQUIRE(editor.score().measures[0].beats.size() == 4);

    while (editor.undoStack().canUndo()) {
        editor.undo();
    }
    REQUIRE(beatAt(editor.score(), first).dur == model::Dur::Quarter);
    REQUIRE(beatAt(editor.score(), first).notes[0].fret == 3);
    REQUIRE_FALSE(beatAt(editor.score(), first).notes[0].tieToNext);
}
