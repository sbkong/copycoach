// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 3.6 completion criteria for module 01.
#include "core/theory/chord.h"
#include "core/theory/scaleform.h"
#include "fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <utility>

using namespace cc::theory;

namespace
{

const Tuning &standardTuning()
{
    static const Tuning tuning = Tuning::standardSix();
    return tuning;
}

PitchClass pitchClassNamed(const char *name)
{
    const auto parsed = parsePitchClass(name);
    REQUIRE(parsed.has_value());
    return parsed.value_or(0);
}

ScaleGroup groupForLetter(const std::string &letter)
{
    if (letter == "R") {
        return ScaleGroup::Root;
    }
    if (letter == "Y") {
        return ScaleGroup::SevenOneTwo;
    }
    return ScaleGroup::ThreeToSix;
}

std::set<std::pair<int, int>> positionsOf(const std::vector<ScaleNote> &notes)
{
    std::set<std::pair<int, int>> out;
    for (const ScaleNote &n : notes) {
        out.insert({n.position.string, n.position.fret});
    }
    return out;
}

/// Fixture form notes are [string, fret, label, group] with group R / Y (7-1-2) / B (3-4-5-6).
std::set<std::pair<int, int>> fixturePositions(const nlohmann::json &form)
{
    std::set<std::pair<int, int>> out;
    for (const auto &note : form["notes"]) {
        out.insert({note[0].get<int>(), note[1].get<int>()});
    }
    return out;
}

} // namespace

TEST_CASE("scale: C major diagonal forms equal the source page (section 3.2 expected values)")
{
    const auto fixture = cc::test::loadRoutineFixture("app-features");
    int matched = 0;
    for (const auto &form : fixture["forms"]) {
        const std::string id = form["id"];
        const RootString rootOn = id == "scale-5a" ? RootString::Five : RootString::Six;
        const auto generated = diagonalMajorForm(0, rootOn, standardTuning());
        INFO(id);
        REQUIRE(generated.size() == 21);
        REQUIRE(positionsOf(generated) == fixturePositions(form));
        for (const auto &note : form["notes"]) {
            const StringFret position{note[0].get<int>(), note[1].get<int>()};
            for (const ScaleNote &n : generated) {
                if (n.position == position) {
                    REQUIRE(n.group == groupForLetter(note[3]));
                }
            }
        }
        ++matched;
    }
    REQUIRE(matched == 2);
}

TEST_CASE("scale: 12 keys x 2 root strings ascend in pitch, cover the scale, alternate 3/4-note groups (criteria 1-2)")
{
    const Tuning &tuning = standardTuning();
    int highestFret = 0;
    for (int key = 0; key < 12; ++key) {
        for (const RootString rootOn : {RootString::Five, RootString::Six}) {
            const auto form = diagonalMajorForm(key, rootOn, tuning);
            INFO("key " << key << " rootOn " << static_cast<int>(rootOn));
            REQUIRE(form.size() == 21);
            Midi previous = -1;
            for (const ScaleNote &n : form) {
                REQUIRE(tuning.pitchAt(n.position) > previous);
                previous = tuning.pitchAt(n.position);
                REQUIRE(tuning.pitchClassAt(n.position) == degreePitchClass(key, n.parentDegree));
                REQUIRE(n.position.fret >= 0);
                highestFret = std::max(highestFret, n.position.fret);
            }
            for (int string = 6; string >= 1; --string) {
                int count = 0;
                for (const ScaleNote &n : form) {
                    count += n.position.string == string ? 1 : 0;
                }
                const bool threeNoteString = (rootOn == RootString::Six) == ((6 - string) % 2 == 0);
                REQUIRE(count == (threeNoteString ? 3 : 4));
            }
        }
    }
    REQUIRE(highestFret
            <= 22); // the form may climb to fret 21 (e.g. Eb/E keys); the fretboard shows 0..22 like the source app
}

TEST_CASE("scale: modes map to a parent major and label degrees from the practised tonic (section 3.2-3.3)")
{
    REQUIRE(parentMajorOf(pitchClassNamed("Gb"), 7) == pitchClassNamed("G")); // Gb Locrian = G major
    REQUIRE(parentMajorOf(pitchClassNamed("D"), 2) == 0); // D Dorian = C major
    REQUIRE(parentMajorOf(5, 1) == 5);
    REQUIRE(modeDegreeOf(7, 7) == 1); // parent degree 7 is the Locrian tonic
    REQUIRE(modeDegreeOf(1, 7) == 2);
    REQUIRE(modeDegreeOf(6, 7) == 7);
    REQUIRE(modeDegreeOf(3, 1) == 3);
}
