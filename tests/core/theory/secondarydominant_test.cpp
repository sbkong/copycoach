// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 2.10 criterion 5: the secondary dominant mode of module 02 (sections 2.5 and 2.6).
#include "core/theory/shellvoicing.h"
#include "fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace cc::theory;

namespace
{

const Tuning &standardTuning()
{
    static const Tuning tuning = Tuning::standardSix();
    return tuning;
}

std::set<std::pair<int, int>> asSet(const std::vector<StringFret> &positions)
{
    std::set<std::pair<int, int>> out;
    for (const StringFret &p : positions) {
        out.insert({p.string, p.fret});
    }
    return out;
}

std::vector<StringFret> voicing(const ShellSlot &slot, PitchClass key, TensionRule rule, bool upper)
{
    std::vector<StringFret> notes = shellNotes(slot, standardTuning());
    if (upper) {
        for (const UpperNote &u : secondaryUpperNotes(slot, key, rule, standardTuning())) {
            notes.push_back(u.position);
        }
    }
    return notes;
}

/// Slot index of a fixture card titled "... (V7/ii)" or "... (V7, ...)"; -1 for the other cards.
int slotOfCard(const std::string &title)
{
    static const std::map<std::string, int> bySuffix{{"(V7/IV)", 0},
                                                     {"(V7/iii)", 2},
                                                     {"(V7/vi)", 3},
                                                     {"(V7/ii)", 4},
                                                     {"(V7/V)", 5},
                                                     {"(V7,", 6}};
    for (const auto &[tag, index] : bySuffix) {
        if (title.contains(tag)) {
            return index;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("secondary dominant: C major shells match the spec table and the fixture cards (section 2.5)")
{
    const auto slots = secondaryDominantLoop(0, standardTuning());
    const std::vector<std::string>
        expectedTabs{"8 x 8 9", "x 8 7 9", "7 x 7 8", "x 7 6 7", "5 x 5 6", "x 5 4 5", "3 x 3 4", "x 3 2 4"};
    const std::vector<std::string> expectedNames{"C7", "Fmaj7", "B7", "E7", "A7", "D7", "G7", "Cmaj7"};
    const std::vector<int> expectedTargets{4, 0, 3, 6, 2, 5, 0, 0};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        INFO("slot " << i + 1);
        REQUIRE(chordName(slots[i].rootPitchClass, slots[i].quality) == expectedNames[i]);
        REQUIRE(slots[i].target == expectedTargets[i]);
        REQUIRE(asSet(shellNotes(slots[i], standardTuning())) == asSet(cc::test::positionsFromTab(expectedTabs[i])));
    }

    const auto fixture = cc::test::loadRoutineFixture("secondary-dominant");
    int shellCards = 0;
    for (const auto &card : fixture["cards"]) {
        const std::string title = card["title"];
        const int slot = slotOfCard(title);
        if (slot < 0 || card["notes"].size() == 5) {
            continue; // tension cards are checked in the section 2.6 test
        }
        INFO(title);
        REQUIRE(asSet(shellNotes(slots[static_cast<std::size_t>(slot)], standardTuning()))
                == asSet(cc::test::playedPositions(card["notes"])));
        ++shellCards;
    }
    REQUIRE(shellCards == 6); // five turned slots plus the kept G7
}

TEST_CASE(
    "secondary dominant: C major tensions, chromatic from the fixture, diatonic differs only on V7/ii "
    "(section 2.6)")
{
    const auto slots = secondaryDominantLoop(0, standardTuning());
    const auto fixture = cc::test::loadRoutineFixture("secondary-dominant");
    int tensionCards = 0;
    for (const auto &card : fixture["cards"]) {
        const std::string title = card["title"];
        const int slot = slotOfCard(title);
        if (slot < 0 || card["notes"].size() != 5) {
            continue;
        }
        INFO(title);
        const auto chromatic = voicing(slots[static_cast<std::size_t>(slot)], 0, TensionRule::Chromatic, true);
        REQUIRE(asSet(chromatic) == asSet(cc::test::positionsFromTab(card["fing"])));
        ++tensionCards;
    }
    REQUIRE(tensionCards == 6);

    for (std::size_t i = 0; i < slots.size(); ++i) {
        INFO("slot " << i + 1);
        const auto diatonic = voicing(slots[i], 0, TensionRule::Diatonic, true);
        const auto chromatic = voicing(slots[i], 0, TensionRule::Chromatic, true);
        if (i == 4) {
            REQUIRE(asSet(diatonic) == asSet(cc::test::positionsFromTab("5 x 5 6 6 7"))); // A7(b13)
            REQUIRE(asSet(chromatic) == asSet(cc::test::positionsFromTab("5 x 5 6 6 6"))); // A7(b9 b13)
        } else {
            REQUIRE(asSet(diatonic) == asSet(chromatic));
        }
    }
    // Slots II and VIII keep the diatonic upper structure of section 2.4.
    REQUIRE(asSet(voicing(slots[1], 0, TensionRule::Diatonic, true))
            == asSet(cc::test::positionsFromTab("x 8 7 9 8 8")));
    REQUIRE(asSet(voicing(slots[7], 0, TensionRule::Diatonic, true))
            == asSet(cc::test::positionsFromTab("x 3 2 4 3 3")));
}

TEST_CASE("secondary dominant: all 12 keys turn slots 1 3 4 5 6 into V7 of the next chord within reach")
{
    const Tuning &tuning = standardTuning();
    const std::set<std::size_t> turned{0, 2, 3, 4, 5};
    for (int key = 0; key < 12; ++key) {
        const auto diatonic = shellLoop(key, LoopOrder::Circle5, tuning);
        const auto slots = secondaryDominantLoop(key, tuning);
        int differingNotes = 0;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            INFO("key " << key << " slot " << i + 1);
            REQUIRE(slots[i].root == diatonic[i].root);
            REQUIRE((slots[i].target != 0) == turned.contains(i));
            if (slots[i].target != 0) {
                REQUIRE(slots[i].quality == ChordQuality::Dom7);
                REQUIRE(slots[i].target == slots[i + 1].degree);
            }
            std::set<int> pitchClasses;
            for (const StringFret &p : shellNotes(slots[i], tuning)) {
                pitchClasses.insert(tuning.pitchClassAt(p));
            }
            const std::set<int> expected{
                slots[i].rootPitchClass,
                transpose(slots[i].rootPitchClass, chordToneInterval(slots[i].quality, ChordTone::Third)),
                transpose(slots[i].rootPitchClass, chordToneInterval(slots[i].quality, ChordTone::Seventh))};
            REQUIRE(pitchClasses == expected);
            for (const TensionRule rule : {TensionRule::Diatonic, TensionRule::Chromatic}) {
                for (const UpperNote &u : secondaryUpperNotes(slots[i], key, rule, tuning)) {
                    REQUIRE(tuning.pitchClassAt(u.position)
                            == transpose(slots[i].rootPitchClass, chordToneInterval(slots[i].quality, u.tone)));
                    REQUIRE(std::abs(u.position.fret - slots[i].root.fret) <= 4);
                }
            }
            // The two rules differ in one note only, the 9 of V7/ii (on string 1 for a 6th-string root, string 2 for
            // a 5th-string root).
            const auto a = secondaryUpperNotes(slots[i], key, TensionRule::Diatonic, tuning);
            const auto b = secondaryUpperNotes(slots[i], key, TensionRule::Chromatic, tuning);
            for (std::size_t n = 0; n < a.size(); ++n) {
                if (a[n].position != b[n].position) {
                    REQUIRE(i == 4);
                    REQUIRE(a[n].tone == ChordTone::Ninth);
                    REQUIRE(b[n].tone == ChordTone::FlatNinth);
                    ++differingNotes;
                }
            }
        }
        REQUIRE(differingNotes == 1);
    }
}
