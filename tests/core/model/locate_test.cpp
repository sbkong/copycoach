// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/locate.h"
#include "core/model/scorebuilder.h"

#include <catch2/catch_test_macros.hpp>

using namespace cc;
using namespace cc::model;

namespace
{

/// Two 4/4 measures of quarter notes, then a 3/4 measure with one half note and a rest.
Score sample()
{
    IdGenerator ids = IdGenerator::seeded(4);
    ScoreSpec spec;
    const MeasureSpec quarters{
        {{Dur::Quarter, {{1, 3}}}, {Dur::Quarter, {{1, 5}}}, {Dur::Quarter, {{1, 7}}}, {Dur::Quarter, {{1, 8}}}}};
    spec.measures = {quarters, quarters, MeasureSpec{{{Dur::Half, {{2, 3}}}, {Dur::Quarter, {}}}}};
    Score score = buildScore(spec, ids);
    score.measures[2].timeSig = TimeSignature{3, 4};
    return score;
}

/// locateBeat that fails the test instead of handing back nothing.
LocatedBeat at(const Score &score, Tick tick, int voice = 1)
{
    const auto located = locateBeat(score, tick, 0, voice);
    REQUIRE(located.has_value());
    return located.value_or(LocatedBeat{});
}

} // namespace

TEST_CASE("locate: the measure under a tick follows the meter changes")
{
    const Score score = sample();
    REQUIRE(measureAtTick(score, 0) == 0);
    REQUIRE(measureAtTick(score, 3839) == 0);
    REQUIRE(measureAtTick(score, 3840) == 1);
    REQUIRE(measureAtTick(score, 7680) == 2);
    REQUIRE(measureAtTick(score, 7680 + 2879) == 2);
    REQUIRE_FALSE(measureAtTick(score, 7680 + 2880)); // past the 3/4 measure
    REQUIRE_FALSE(measureAtTick(score, -1));
}

TEST_CASE("locate: the beat under a tick, its start and where the next one begins")
{
    const Score score = sample();
    const LocatedBeat second = at(score, 3840 + 1000);
    REQUIRE(second.measure == 1);
    REQUIRE(second.startTick == 3840 + 960);
    REQUIRE(second.nextTick == 3840 + 1920);
    REQUIRE(second.beat->notes[0].fret == 5);

    const LocatedBeat last = at(score, 3840 + 3000);
    REQUIRE(last.nextTick == 7680); // the next beat is the first of measure 3: the measure end says the same

    const LocatedBeat half = at(score, 7680 + 100);
    REQUIRE(half.measure == 2);
    REQUIRE(half.nextTick == 7680 + 1920); // the rest
    const LocatedBeat rest = at(score, 7680 + 2000);
    REQUIRE(rest.beat->rest);
    REQUIRE(rest.nextTick == 7680 + 2880); // end of the 3/4 measure

    REQUIRE_FALSE(locateBeat(score, 100, 0, 2)); // no second voice
    REQUIRE_FALSE(locateBeat(score, 20000, 0));
}

TEST_CASE("locate: a tick in a measure that has no beat of the voice falls back to the previous measure")
{
    Score score = sample();
    score.measures[1].beats.clear();
    const LocatedBeat located = at(score, 3840 + 500);
    REQUIRE(located.measure == 0);
    REQUIRE(located.startTick == 2880); // the last quarter of measure 1
    REQUIRE(located.nextTick == 3840);
}
