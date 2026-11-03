// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/duration.h"
#include "core/model/id.h"
#include "core/model/scorebuilder.h"
#include "core/model/timemap.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace cc::model;

TEST_CASE("duration: ticks follow 960 per quarter with dots and tuplets")
{
    REQUIRE(durationTicks(Dur::Quarter) == 960);
    REQUIRE(durationTicks(Dur::Whole) == 3840);
    REQUIRE(durationTicks(Dur::Eighth) == 480);
    REQUIRE(durationTicks(Dur::Quarter, 1) == 1440);
    REQUIRE(durationTicks(Dur::Quarter, 2) == 1680);
    REQUIRE(durationTicks(Dur::Eighth, 0, Tuplet{3, 2}) == 320);
    REQUIRE(measureTicks(TimeSignature{4, 4}) == 3840);
    REQUIRE(measureTicks(TimeSignature{3, 4}) == 2880);
    REQUIRE(measureTicks(TimeSignature{6, 8}) == 2880);
    REQUIRE(beatDur(TimeSignature{6, 8}) == Dur::Eighth);
}

TEST_CASE("id: seeded generators are deterministic and ids are 8 base32 characters")
{
    IdGenerator a = IdGenerator::seeded(7);
    IdGenerator b = IdGenerator::seeded(7);
    std::set<Id> seen;
    for (int i = 0; i < 1000; ++i) {
        const Id id = a.next();
        REQUIRE(id == b.next());
        REQUIRE(isWellFormedId(id));
        seen.insert(id);
    }
    REQUIRE(seen.size() == 1000);
    REQUIRE_FALSE(isWellFormedId("ABCDEFGH"));
    REQUIRE_FALSE(isWellFormedId("abc"));
}

TEST_CASE("score: builder packs beats into measures and start ticks follow time signature changes")
{
    IdGenerator ids = IdGenerator::seeded(1);
    ScoreSpec spec;
    spec.timeSig = TimeSignature{4, 4};
    spec.measures = {
        MeasureSpec{{BeatSpec{Dur::Quarter, {{6, 8}}, false},
                     BeatSpec{Dur::Half, {{5, 8}, {4, 9}}, false},
                     BeatSpec{Dur::Quarter, {}, false}}},
        MeasureSpec{{BeatSpec{Dur::Whole, {{6, 3}}, false}}},
    };
    Score score = buildScore(spec, ids);
    REQUIRE(score.measures.size() == 2);
    REQUIRE(score.measures[0].beats[1].tick == 960);
    REQUIRE(score.measures[0].beats[2].tick == 2880);
    REQUIRE(score.measures[0].beats[2].rest);
    REQUIRE(score.measures[0].beats[1].notes.size() == 2);
    REQUIRE(score.measureStartTick(1) == 3840);
    REQUIRE(score.totalTicks() == 7680);

    score.measures[1].timeSig = TimeSignature{3, 4};
    Measure third;
    third.id = ids.next();
    score.measures.push_back(third);
    REQUIRE(score.timeSigAt(0) == TimeSignature{4, 4});
    REQUIRE(score.timeSigAt(2) == TimeSignature{3, 4});
    REQUIRE(score.measureStartTick(2) == 3840 + 2880);
}

TEST_CASE("score: tracks carry a role, key and swing change per measure, sections anchor on a measure")
{
    IdGenerator ids = IdGenerator::seeded(3);
    ScoreSpec spec;
    spec.measures = {MeasureSpec{}, MeasureSpec{}, MeasureSpec{}, MeasureSpec{}};
    Score score = buildScore(spec, ids);
    REQUIRE(score.tracks.size() == 1);
    REQUIRE(score.primaryTrack().role == TrackRole::Other);
    REQUIRE(score.measures[0].beats.empty());

    score.tracks.push_back(Track{ids.next(), "Melody", cc::theory::Tuning::standardSix(), TrackRole::Melody});
    Beat beat;
    beat.id = ids.next();
    beat.track = 1;
    beat.notes.push_back(Note{ids.next(), 1, 3, false});
    score.measures[0].beats.push_back(beat);
    REQUIRE(score.tracks.at(score.measures[0].beats[0].track).role == TrackRole::Melody);

    REQUIRE(score.keySigAt(0) == KeySignature{});
    REQUIRE(score.swingAt(0) == straightSwing);
    score.measures[1].keySig = KeySignature{-2, false}; // Bb major
    score.measures[2].swingRatio = 2.0 / 3.0;
    REQUIRE(score.keySigAt(0) == KeySignature{});
    REQUIRE(score.keySigAt(1) == KeySignature{-2, false});
    REQUIRE(score.keySigAt(3) == KeySignature{-2, false});
    REQUIRE(score.swingAt(1) == straightSwing);
    REQUIRE(score.swingAt(3) == 2.0 / 3.0);

    score.sections = {Section{ids.next(), 1, "A", "Head"}, Section{ids.next(), 3, "B", "Solo"}};
    REQUIRE_FALSE(score.sectionIndexAt(0));
    REQUIRE(score.sectionIndexAt(1) == 0);
    REQUIRE(score.sectionIndexAt(2) == 0);
    REQUIRE(score.sectionIndexAt(3) == 1);

    score.measures[1].chords = {ChordSymbol{ids.next(), 0, "Dm7"}, ChordSymbol{ids.next(), 1920, "G7b9"}};
    REQUIRE(score.measures[1].chords[1].tick == 1920);
    REQUIRE(score.measures[1].chords[1].text == "G7b9");
}

TEST_CASE("timemap: ticks and samples round-trip, tempo changes start new segments")
{
    IdGenerator ids = IdGenerator::seeded(2);
    ScoreSpec spec;
    spec.tempoBpm = 60.0;
    spec.measures = {MeasureSpec{}, MeasureSpec{}, MeasureSpec{}};
    Score score = buildScore(spec, ids);
    score.measures[1].tempoBpm = 120.0;
    const TimeMap map = TimeMap::fromScore(score, 48000.0);
    REQUIRE(map.segments().size() == 2);
    REQUIRE(map.tickToSample(960) == 48000); // one quarter at 60 bpm = one second
    REQUIRE(map.tickToSample(3840) == 192000); // measure 2 starts after 4 s
    REQUIRE(map.segments()[1].startSample == 192000);
    REQUIRE(map.tickToSample(3840 + 960) == 192000 + 24000); // quarter at 120 bpm = half a second
    for (const Tick tick : {0, 1, 959, 960, 3839, 3840, 5000, 7680}) {
        REQUIRE(map.sampleToTick(map.tickToSample(tick)) == tick);
    }
    REQUIRE(map.sampleToTick(SampleIndex{48000} * 3) == 2880);
}
