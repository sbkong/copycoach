// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Chord symbol text -> root, kind, extension, alterations, bass (copy-spec.md section 16.2 A1-2).
#include "core/theory/chordsymbol.h"
#include "core/theory/fingering.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cc::theory;

namespace
{

ParsedChordSymbol parsed(const char *text)
{
    const auto chord = parseChordSymbol(text);
    REQUIRE(chord);
    return chord.value_or(ParsedChordSymbol{}); // REQUIRE already failed the test when empty
}

constexpr PitchClass c = 0;
constexpr PitchClass d = 2;
constexpr PitchClass e = 4;
constexpr PitchClass f = 5;
constexpr PitchClass g = 7;
constexpr PitchClass a = 9;
constexpr PitchClass b = 11;

} // namespace

TEST_CASE("chord symbol: roots with accidentals, triads and sixths")
{
    REQUIRE(parsed("C").root == c);
    REQUIRE(parsed("C").kind == ChordKind::Major);
    REQUIRE(parsed("C").extension == 5);
    REQUIRE(parsed("F#").root == 6);
    REQUIRE(parsed("Bb").root == 10);
    REQUIRE(parsed("Cb").root == b);
    REQUIRE(parsed("E♭").root == 3);
    REQUIRE(parsed("Am").kind == ChordKind::Minor);
    REQUIRE(parsed("A-").kind == ChordKind::Minor);
    REQUIRE(parsed("Amin").kind == ChordKind::Minor);
    REQUIRE(parsed("C6").extension == 6);
    REQUIRE(parsed("Cm6").kind == ChordKind::Minor);
    REQUIRE(parsed("Cm6").extension == 6);
    const ParsedChordSymbol sixNine = parsed("C6/9");
    REQUIRE(sixNine.extension == 6);
    REQUIRE(sixNine.alterations == std::vector<ChordAlteration>{{9, 0}});
    REQUIRE(parsed("C69").alterations == std::vector<ChordAlteration>{{9, 0}});
}

TEST_CASE("chord symbol: sevenths tell major, minor, dominant and minor-major apart")
{
    const ParsedChordSymbol maj7 = parsed("Cmaj7");
    REQUIRE(maj7.kind == ChordKind::Major);
    REQUIRE(maj7.extension == 7);
    REQUIRE(maj7.majorSeventh);
    REQUIRE(parsed("CM7").majorSeventh);
    REQUIRE(parsed("Cma7").majorSeventh);
    REQUIRE(parsed("CΔ").extension == 7);
    REQUIRE(parsed("CΔ").majorSeventh);
    REQUIRE(parsed("Cmaj9").extension == 9);
    const ParsedChordSymbol dom = parsed("G7");
    REQUIRE(dom.root == g);
    REQUIRE(dom.kind == ChordKind::Dominant);
    REQUIRE(dom.extension == 7);
    REQUIRE_FALSE(dom.majorSeventh);
    REQUIRE(parsed("G9").extension == 9);
    REQUIRE(parsed("G13").kind == ChordKind::Dominant);
    REQUIRE(parsed("G13").extension == 13);
    const ParsedChordSymbol min7 = parsed("Dm7");
    REQUIRE(min7.root == d);
    REQUIRE(min7.kind == ChordKind::Minor);
    REQUIRE(min7.extension == 7);
    REQUIRE(parsed("D-7").kind == ChordKind::Minor);
    REQUIRE(parsed("Dmi7").kind == ChordKind::Minor);
    REQUIRE(parsed("Dm11").extension == 11);
    const ParsedChordSymbol minMaj = parsed("CmMaj7");
    REQUIRE(minMaj.kind == ChordKind::Minor);
    REQUIRE(minMaj.majorSeventh);
    REQUIRE(parsed("Cm(maj7)").majorSeventh);
    REQUIRE(parsed("C-Δ7").majorSeventh);
}

TEST_CASE("chord symbol: half-diminished, diminished, augmented and suspended")
{
    const ParsedChordSymbol half = parsed("Bm7b5");
    REQUIRE(half.root == b);
    REQUIRE(half.kind == ChordKind::HalfDiminished);
    REQUIRE(half.extension == 7);
    REQUIRE(half.alterations.empty());
    REQUIRE(parsed("Bø").kind == ChordKind::HalfDiminished);
    REQUIRE(parsed("Bø").extension == 7);
    REQUIRE(parsed("Bø7").kind == ChordKind::HalfDiminished);
    REQUIRE(parsed("Bdim").kind == ChordKind::Diminished);
    REQUIRE(parsed("Bdim").extension == 5);
    REQUIRE(parsed("Bdim7").extension == 7);
    REQUIRE(parsed("B°7").kind == ChordKind::Diminished);
    REQUIRE(parsed("Bo7").kind == ChordKind::Diminished);
    REQUIRE(parsed("Caug").kind == ChordKind::Augmented);
    REQUIRE(parsed("C+").kind == ChordKind::Augmented);
    REQUIRE(parsed("Dsus4").kind == ChordKind::Suspended);
    REQUIRE(parsed("Dsus4").susDegree == 4);
    REQUIRE(parsed("Dsus").susDegree == 4);
    REQUIRE(parsed("Dsus2").susDegree == 2);
    const ParsedChordSymbol sevenSus = parsed("D7sus4");
    REQUIRE(sevenSus.kind == ChordKind::Suspended);
    REQUIRE(sevenSus.extension == 7);
    REQUIRE(sevenSus.susDegree == 4);
}

TEST_CASE("chord symbol: tensions in any spelling, alt, add and slash bass")
{
    REQUIRE(parsed("G7b9").alterations == std::vector<ChordAlteration>{{9, -1}});
    REQUIRE(parsed("G7(b9)").alterations == std::vector<ChordAlteration>{{9, -1}});
    REQUIRE(parsed("G7-9").alterations == std::vector<ChordAlteration>{{9, -1}});
    REQUIRE(parsed("G7#9").alterations == std::vector<ChordAlteration>{{9, 1}});
    REQUIRE(parsed("G7#5").alterations == std::vector<ChordAlteration>{{5, 1}});
    REQUIRE(parsed("G7+5").alterations == std::vector<ChordAlteration>{{5, 1}});
    const ParsedChordSymbol stacked = parsed("G7(b9,#11)");
    REQUIRE(stacked.alterations == std::vector<ChordAlteration>{{9, -1}, {11, 1}});
    REQUIRE(parsed("G13b9").extension == 13);
    REQUIRE(parsed("G7alt").altered);
    REQUIRE(parsed("G7alt").kind == ChordKind::Dominant);
    REQUIRE(parsed("Ebmaj7#11").alterations == std::vector<ChordAlteration>{{11, 1}});
    REQUIRE(parsed("Cadd9").alterations == std::vector<ChordAlteration>{{9, 0}});
    REQUIRE(parsed("Cadd9").extension == 5);
    const ParsedChordSymbol slash = parsed("F/A");
    REQUIRE(slash.root == f);
    REQUIRE(slash.bass == a);
    REQUIRE(parsed("Bb7/D").bass == d);
    REQUIRE(parsed("Bb7/D").kind == ChordKind::Dominant);
    REQUIRE(parsed("C/E").bass == e);
}

TEST_CASE("chord symbol: what is not a symbol stays text")
{
    REQUIRE_FALSE(parseChordSymbol(""));
    REQUIRE_FALSE(parseChordSymbol("H7"));
    REQUIRE_FALSE(parseChordSymbol("C/"));
    REQUIRE_FALSE(parseChordSymbol("Cmaj7x"));
    REQUIRE_FALSE(parseChordSymbol("C8"));
    REQUIRE_FALSE(parseChordSymbol("N.C."));
    REQUIRE_FALSE(parseChordSymbol("c7"));
}

TEST_CASE("fingering pattern: names round-trip and unknown names are rejected")
{
    REQUIRE(fingeringPatternName(FingeringPattern::Vertical) == "vertical");
    REQUIRE(fingeringPatternName(FingeringPattern::Diagonal) == "diagonal");
    REQUIRE(parseFingeringPattern("horizontal") == FingeringPattern::Horizontal);
    REQUIRE(parseFingeringPattern("diagonal") == FingeringPattern::Diagonal);
    REQUIRE_FALSE(parseFingeringPattern("block"));
}
