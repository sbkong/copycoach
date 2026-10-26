// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 2.10 completion criteria 1-3 for module 02.
#include "core/theory/shellvoicing.h"
#include "fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
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

std::set<std::pair<int, int>> asSet(const std::vector<StringFret> &positions)
{
    std::set<std::pair<int, int>> out;
    for (const StringFret &p : positions) {
        out.insert({p.string, p.fret});
    }
    return out;
}

std::vector<StringFret> fullVoicing(const ShellSlot &slot)
{
    std::vector<StringFret> notes = shellNotes(slot, standardTuning());
    for (const UpperNote &u : upperNotes(slot, standardTuning())) {
        notes.push_back(u.position);
    }
    return notes;
}

std::string circledDigit(std::size_t index) // 0 -> "①" ... 7 -> "⑧" (U+2460..)
{
    return std::string("\xE2\x91") + static_cast<char>(0xA0 + static_cast<int>(index));
}

} // namespace

TEST_CASE("shell: C major circle-of-fifths loop roots match the spec (section 2.2 expected values)")
{
    const auto slots = shellLoop(0, LoopOrder::Circle5, standardTuning());
    const std::vector<StringFret> expected{{6, 8}, {5, 8}, {6, 7}, {5, 7}, {6, 5}, {5, 5}, {6, 3}, {5, 3}};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        INFO("slot " << i + 1);
        REQUIRE(slots[i].root == expected[i]);
    }
    REQUIRE(chordName(slots[2].rootPitchClass, slots[2].quality) == "Bm7b5");
    REQUIRE(chordName(slots[6].rootPitchClass, slots[6].quality) == "G7");
}

TEST_CASE("shell: C major shell + upper structure equals the app loop cards (section 2.4 expected values)")
{
    const auto fixture = cc::test::loadRoutineFixture("upper-structure");
    const auto slots = shellLoop(0, LoopOrder::Circle5, standardTuning());
    int matched = 0;
    for (const auto &card : fixture["cards"]) {
        const std::string title = card["title"];
        // The loop cards are titled with circled digits followed by "chord → chord with tension", e.g. "① Cmaj7 →
        // Cmaj9".
        for (std::size_t i = 0; i < 8; ++i) {
            if (!title.starts_with(circledDigit(i)) || card["notes"].size() != 5 || !title.contains("→")) {
                continue;
            }
            INFO(title);
            REQUIRE(asSet(fullVoicing(slots[i])) == asSet(cc::test::playedPositions(card["notes"])));
            REQUIRE(asSet(fullVoicing(slots[i])) == asSet(cc::test::positionsFromTab(card["fing"])));
            ++matched;
        }
    }
    REQUIRE(matched == 6); // ①②⑤⑥⑦⑧ carry tensions; ③④ (iii, vii) are tested below
}

TEST_CASE("shell: iii and vii slots drop the ninth and take root + fifth (section 2.4)")
{
    const auto slots = shellLoop(0, LoopOrder::Circle5, standardTuning());
    REQUIRE(asSet(fullVoicing(slots[2])) == asSet(cc::test::positionsFromTab("7 x 7 7 6 7"))); // Bm7b5
    REQUIRE(asSet(fullVoicing(slots[3])) == asSet(cc::test::positionsFromTab("x 7 5 7 5 7"))); // Em7
}

TEST_CASE("shell: all 12 keys x 2 orders produce correct chord tones in one position (criteria 1-3)")
{
    const Tuning &tuning = standardTuning();
    for (int key = 0; key < 12; ++key) {
        for (const LoopOrder order : {LoopOrder::Circle5, LoopOrder::Scale}) {
            const auto slots = shellLoop(key, order, tuning);
            REQUIRE(slots[7].degree == 1);
            for (const ShellSlot &slot : slots) {
                INFO("key " << key << " order " << static_cast<int>(order) << " slot " << slot.index);
                REQUIRE((slot.root.string == 6 || slot.root.string == 5));
                REQUIRE(slot.root.fret >= 1);
                REQUIRE(slot.root.fret <= 12);
                const auto shell = shellNotes(slot, tuning);
                REQUIRE(shell.size() == 3);
                std::set<int> pitchClasses;
                for (const StringFret &p : shell) {
                    pitchClasses.insert(tuning.pitchClassAt(p));
                }
                const std::set<int> expected{
                    slot.rootPitchClass,
                    transpose(slot.rootPitchClass, chordToneInterval(slot.quality, ChordTone::Third)),
                    transpose(slot.rootPitchClass, chordToneInterval(slot.quality, ChordTone::Seventh))};
                REQUIRE(pitchClasses == expected);
                for (const UpperNote &u : upperNotes(slot, tuning)) {
                    REQUIRE(tuning.pitchClassAt(u.position)
                            == transpose(slot.rootPitchClass, chordToneInterval(slot.quality, u.tone)));
                    REQUIRE(std::abs(u.position.fret - slot.root.fret) <= 4);
                }
            }
            if (order == LoopOrder::Circle5) {
                for (std::size_t i = 1; i < slots.size(); ++i) {
                    REQUIRE(slots[i].root.string != slots[i - 1].root.string); // strings alternate 6/5
                }
            }
        }
    }
}
