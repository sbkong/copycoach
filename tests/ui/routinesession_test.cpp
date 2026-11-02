// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// RoutineSession: slotList for the three implemented modules, selection, mix masks, score building.
#include "ui/routine/routinesession.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QStringList>

#include <algorithm>

using namespace cc;
using namespace cc::ui;

namespace
{

RoutineSession makeSession()
{
    routine::RoutineState state;
    state.moduleId = "02";
    state.modeId = "diatonic-loop";
    return {routine::builtInRoutineDefinition(), state};
}

} // namespace

TEST_CASE("routine session: module 02 in C gives the eight shell slotList with roman numerals and colours")
{
    RoutineSession session = makeSession();
    REQUIRE(session.slotList().size() == 8);
    REQUIRE(session.slotList()[0].name == QStringLiteral("Cmaj7"));
    REQUIRE(session.slotList()[0].roman == QStringLiteral("Imaj7"));
    REQUIRE(session.slotList()[0].rootString == QStringLiteral("6th"));
    REQUIRE(session.slotList()[0].chordColor == theme::Token::ChordCColor);
    REQUIRE(session.slotList()[1].name == QStringLiteral("Fmaj7"));
    REQUIRE(session.slotList()[6].name == QStringLiteral("G7"));
    REQUIRE(session.slotList()[6].chordColor == theme::Token::ChordGColor);
    REQUIRE(session.slotList()[0].notes.size() == 3); // upper off by default
    REQUIRE(session.slotList()[0].notes[0].isRoot);
    REQUIRE(session.slotList()[0].notes[0].degreeLabel == QStringLiteral("R"));
    REQUIRE(session.state().options.at("order") == "circle5");

    session.setOption("upper", "on");
    REQUIRE(session.slotList()[0].notes.size() == 5);
    REQUIRE(session.slotList()[0].notes[3].kind == DotKind::Upper);
    REQUIRE(session.slotList()[0].notes[3].degreeLabel == QStringLiteral("5"));
    REQUIRE(session.slotList()[0].notes[4].degreeLabel == QStringLiteral("9"));
}

TEST_CASE("routine session: secondary dominant mode names the turned slots V7/x and layers tensions")
{
    RoutineSession session = makeSession();
    session.setMode("secondary-dominant");
    REQUIRE(session.state().options.at("upper") == "off");
    const std::vector<Slot> &chords = session.slotList();
    REQUIRE(chords.size() == 8);
    REQUIRE(chords[0].name == QStringLiteral("C7"));
    REQUIRE(chords[0].roman == QStringLiteral("V7/IV"));
    REQUIRE(chords[1].roman == QStringLiteral("IVmaj7"));
    REQUIRE(chords[2].roman == QStringLiteral("V7/iii"));
    REQUIRE(chords[4].name == QStringLiteral("A7"));
    REQUIRE(chords[4].roman == QStringLiteral("V7/ii"));
    REQUIRE(chords[5].roman == QStringLiteral("V7/V"));
    REQUIRE(chords[6].roman == QStringLiteral("V7"));
    REQUIRE(chords[0].chordColor == theme::Token::ChordCColor); // colour stays by scale degree
    REQUIRE(chords[0].notes.size() == 3);

    session.setOption("upper", "chromatic");
    REQUIRE(session.slotList()[4].notes.size() == 5);
    REQUIRE(session.slotList()[4].notes[3].degreeLabel == QStringLiteral("b13"));
    REQUIRE(session.slotList()[4].notes[4].degreeLabel == QStringLiteral("b9"));
    REQUIRE(session.slotList()[4].notes[4].noteName == QStringLiteral("Bb"));
    session.setOption("upper", "diatonic");
    REQUIRE(session.slotList()[4].notes[4].degreeLabel == QStringLiteral("9"));
    REQUIRE(session.slotList()[4].notes[4].noteName == QStringLiteral("B"));
    REQUIRE(session.slotList()[0].notes[3].degreeLabel == QStringLiteral("13"));
}

TEST_CASE("routine session: secondary dominant marks the one shell note it changed as altered (spec 2.5)")
{
    RoutineSession session = makeSession();
    for (const Slot &slot : session.slotList()) {
        for (const SlotNote &note : slot.notes) {
            REQUIRE(!note.altered); // the diatonic loop has nothing outside the key
        }
    }
    session.setMode("secondary-dominant");
    session.setOption("upper", "chromatic");
    const auto alteredNames = [](const Slot &slot) {
        QStringList names;
        for (const SlotNote &note : slot.notes) {
            if (note.altered) {
                names.append(note.noteName);
            }
        }
        return names;
    };
    const std::vector<Slot> &chords = session.slotList();
    REQUIRE(alteredNames(chords[0]) == QStringList{QStringLiteral("Bb")}); // C7: the 7
    REQUIRE(alteredNames(chords[1]).isEmpty()); // Fmaj7 kept
    REQUIRE(alteredNames(chords[2]) == QStringList{QStringLiteral("Eb")}); // B7: the 3 (flat spelling)
    REQUIRE(alteredNames(chords[4]) == QStringList{QStringLiteral("Db")}); // A7: the 3, not the b9 tension
    REQUIRE(alteredNames(chords[6]).isEmpty()); // G7 kept
    REQUIRE(alteredNames(chords[7]).isEmpty());
}

TEST_CASE("routine session: the upper four of module 02 is named as in the original section 4 table (spec 2.7)")
{
    RoutineSession session = makeSession();
    REQUIRE(session.slotList()[0].upperFour.isEmpty()); // upper off: nothing above the shell
    session.setOption("upper", "on");
    const std::vector<Slot> &chords = session.slotList();
    REQUIRE(chords[0].upperFour == QStringLiteral("Em7")); // Cmaj9
    REQUIRE(chords[1].upperFour == QStringLiteral("Am7")); // Fmaj9
    REQUIRE(chords[2].upperFour == QStringLiteral("Dm")); // Bm7b5: triad only
    REQUIRE(chords[3].upperFour == QStringLiteral("G")); // Em7: triad only
    REQUIRE(chords[4].upperFour == QStringLiteral("Cmaj7")); // Am9
    REQUIRE(chords[5].upperFour == QStringLiteral("Fmaj7")); // Dm9
    const QStringList g13 = chords[6].upperFour.split(QStringLiteral("\u00b7"));
    REQUIRE(g13.size() == 4); // G13 spells no chord: the four note names instead
    REQUIRE(g13.contains(QStringLiteral("E")));
    REQUIRE(g13.contains(QStringLiteral("A")));

    session.setMode("secondary-dominant");
    session.setOption("upper", "chromatic");
    REQUIRE(session.slotList()[2].upperFour == QStringLiteral("Am7b5")); // B7(b9 b13) above B
    REQUIRE(session.slotList()[4].upperFour == QStringLiteral("Gm7b5")); // A7(b9 b13) above A
}

TEST_CASE("routine session: the root display option is applied to playback (spec section 1)")
{
    RoutineSession session = makeSession();
    REQUIRE(session.buildScore().measures[0].beats[0].notes.size() == 3);
    const QSignalSpy changed(&session, &RoutineSession::slotsChanged);
    session.setRootDisplay(RootDisplay::Hide);
    REQUIRE(changed.count() == 1);
    REQUIRE(session.slotList()[0].notes.size() == 3); // the dots themselves are untouched
    const auto hidden = session.buildScore().measures[0].beats[0].notes;
    REQUIRE(hidden.size() == 2);
    for (const model::Note &note : hidden) {
        REQUIRE(note.string != 6); // the root sat on string 6
    }
    session.setRootDisplay(RootDisplay::Only);
    const auto only = session.buildScore().measures[0].beats[0].notes;
    REQUIRE(only.size() == 1);
    REQUIRE(only[0].string == 6);
    REQUIRE(rootDisplayOf(QStringLiteral("hide")) == RootDisplay::Hide);
    REQUIRE(rootDisplayOf(QStringLiteral("anything")) == RootDisplay::Show);
}

TEST_CASE("routine session: modules 01 and 03 build their slotList; options reset on mode change")
{
    RoutineSession session = makeSession();
    session.setModule("01");
    REQUIRE(session.state().modeId == "diagonal");
    REQUIRE(session.slotList().size() == 1);
    REQUIRE(session.slotList()[0].isScaleRun);
    REQUIRE(session.slotList()[0].notes.size() == 21);
    REQUIRE(session.slotList()[0].name == QStringLiteral("C 이오니안"));
    session.setOption("mode", "7");
    REQUIRE(session.slotList()[0].name == QStringLiteral("C 로크리안"));
    const auto &notes = session.slotList()[0].notes;
    const auto root = std::ranges::find_if(notes, [](const SlotNote &n) {
        return n.isRoot;
    });
    REQUIRE(root != notes.end());
    REQUIRE(root->degreeLabel == QStringLiteral("1"));
    REQUIRE(root->noteName == QStringLiteral("C")); // labels follow the practised note, not the parent major

    session.setModule("03");
    REQUIRE(session.state().modeId == "closed");
    REQUIRE(session.slotList().size() == 8);
    REQUIRE(session.slotList()[0].name == QStringLiteral("C"));
    REQUIRE(session.slotList()[0].notes.size() == 3);
    session.setMode("open");
    REQUIRE(session.state().options.at("cycle") == "2-7");
    REQUIRE(session.slotList().size() == 8);
}

TEST_CASE("routine session: selection, mute / solo masks and the playable score")
{
    RoutineSession session = makeSession();
    const QSignalSpy selected(&session, &RoutineSession::selectionChanged);
    session.setSelectedSlot(3);
    session.setSelectedSlot(3);
    session.setSelectedSlot(8); // out of range: ignored
    REQUIRE(session.selectedSlot() == 3);
    REQUIRE(selected.count() == 1);

    REQUIRE(session.audibleMask() == 0xFFFFFFFFULL);
    session.toggleMute(1);
    REQUIRE((session.audibleMask() & 0x2ULL) == 0);
    session.toggleSolo(6);
    REQUIRE(session.audibleMask() == 0x40ULL); // solo wins
    session.clearSolo();
    session.muteAll();
    REQUIRE((session.audibleMask() & 0xFFULL) == 0);

    session.setTempo(300.0);
    REQUIRE(session.state().tempoBpm == theory::tempoMaxBpm);
    const model::Score score = session.buildScore();
    REQUIRE(score.measures.size() == 8);
    REQUIRE(score.initialTempoBpm == theory::tempoMaxBpm);
    session.setPlaybackStyle(routine::PlaybackStyle::Arp);
    REQUIRE(session.buildScore().measures[0].beats.size() == 4); // 3 notes + rest
}
