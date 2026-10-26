// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/chord.h"
#include "core/theory/pitch.h"
#include "core/theory/tuning.h"

#include <catch2/catch_test_macros.hpp>

using namespace cc::theory;

TEST_CASE("pitch: pitch class of MIDI numbers and names round-trip")
{
    REQUIRE(pitchClassOf(60) == 0);
    REQUIRE(pitchClassOf(69) == 9);
    REQUIRE(pitchClassOf(-1) == 11);
    REQUIRE(parsePitchClass("C") == 0);
    REQUIRE(parsePitchClass("C#") == 1);
    REQUIRE(parsePitchClass("Db") == 1);
    REQUIRE(parsePitchClass("Bb") == 10);
    REQUIRE(parsePitchClass("Cb") == 11);
    REQUIRE_FALSE(parsePitchClass("H").has_value());
    REQUIRE_FALSE(parsePitchClass("Cx").has_value());
    for (int pc = 0; pc < 12; ++pc) {
        REQUIRE(parsePitchClass(flatName(pc)) == pc);
        REQUIRE(parsePitchClass(sharpName(pc)) == pc);
    }
}

TEST_CASE("tuning: standard six strings, pitch at fret, nearest fret")
{
    const Tuning tuning = Tuning::standardSix();
    REQUIRE(tuning.stringCount() == 6);
    REQUIRE(tuning.pitchAt({1, 0}) == 64);
    REQUIRE(tuning.pitchAt({6, 8}) == 48); // C3
    REQUIRE(tuning.pitchClassAt({6, 8}) == 0);
    REQUIRE(tuning.nearestFret(6, 0, 8) == 8); // C on the low E string near fret 8
    REQUIRE(tuning.nearestFret(6, 0, 3) == 8); // nearest of 8 and 20 to 3 is 8
    REQUIRE(tuning.nearestFret(1, 2, 8) == 10); // D on the high E string: 10 (not 22)
    REQUIRE(tuning.nearestFret(5, 9, 1) == 0); // open A
    Tuning capo = tuning;
    capo.capo = 2;
    REQUIRE(capo.pitchAt({6, 0}) == 42);
}

TEST_CASE("chord: diatonic qualities and chord tone intervals follow the formulas")
{
    REQUIRE(diatonicSeventh(1) == ChordQuality::Maj7);
    REQUIRE(diatonicSeventh(5) == ChordQuality::Dom7);
    REQUIRE(diatonicSeventh(7) == ChordQuality::Min7b5);
    REQUIRE(diatonicTriad(7) == ChordQuality::Diminished);
    REQUIRE(degreePitchClass(0, 5) == 7);
    REQUIRE(degreePitchClass(5, 7) == 4); // F major: vii = E
    REQUIRE(chordToneInterval(ChordQuality::Maj7, ChordTone::Seventh) == 11);
    REQUIRE(chordToneInterval(ChordQuality::Min7b5, ChordTone::Fifth) == 6);
    REQUIRE(chordToneInterval(ChordQuality::Dom7, ChordTone::Thirteenth) == 9);
    REQUIRE(chordName(0, ChordQuality::Maj7) == "Cmaj7");
    REQUIRE(chordName(11, ChordQuality::Min7b5) == "Bm7b5");
    REQUIRE(chordName(10, ChordQuality::Minor) == "Bbm");
    REQUIRE(chordName(11, ChordQuality::Diminished) == "Bdim");
}
