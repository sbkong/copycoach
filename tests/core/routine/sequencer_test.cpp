// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 1 (slot length, Arp / Chord) and section 2.10 criterion 4.
#include "core/routine/sequencer.h"
#include "core/theory/scaleform.h"
#include "core/theory/shellvoicing.h"

#include <catch2/catch_test_macros.hpp>

using namespace cc;
using namespace cc::routine;

namespace
{

model::Score scoreOf(const std::vector<model::MeasureSpec> &measures, model::TimeSignature timeSig)
{
    model::IdGenerator ids = model::IdGenerator::seeded(3);
    model::ScoreSpec spec;
    spec.timeSig = timeSig;
    spec.measures = measures;
    return model::buildScore(spec, ids);
}

std::vector<Voicing> shellSlotVoicings(theory::PitchClass key)
{
    const theory::Tuning tuning = theory::Tuning::standardSix();
    std::vector<Voicing> slots;
    for (const theory::ShellSlot &slot : theory::shellLoop(key, theory::LoopOrder::Circle5, tuning)) {
        slots.push_back(theory::shellNotes(slot, tuning));
    }
    return slots;
}

} // namespace

TEST_CASE("sequencer: Chord style sounds the voicing once per slot for the whole measure")
{
    const auto measures = slotMeasures(shellSlotVoicings(0), model::TimeSignature{4, 4}, PlaybackStyle::Chord);
    REQUIRE(measures.size() == 8);
    const auto events = noteEvents(scoreOf(measures, model::TimeSignature{4, 4}));
    REQUIRE(events.size() == 24U);
    REQUIRE(events[0].onTick == 0);
    REQUIRE(events[0].offTick == 3840);
    REQUIRE(events[3].onTick == 3840); // slot 2 starts at measure 2
    REQUIRE(events[0].pitch == 48); // C3 on string 6 fret 8
}

TEST_CASE("sequencer: Arp style plays one note per beat from the lowest string and rests the remainder")
{
    const auto measures = slotMeasures(shellSlotVoicings(0), model::TimeSignature{4, 4}, PlaybackStyle::Arp);
    const auto events = noteEvents(scoreOf(measures, model::TimeSignature{4, 4}));
    REQUIRE(events.size() == 24U);
    REQUIRE(events[0].onTick == 0);
    REQUIRE(events[1].onTick == 960);
    REQUIRE(events[2].onTick == 1920);
    REQUIRE(events[2].offTick == 2880); // beat 4 is a rest
    REQUIRE(events[3].onTick == 3840);
    REQUIRE(events[0].pitch < events[1].pitch);
    REQUIRE(events[1].pitch < events[2].pitch);
    REQUIRE(measures[0].beats.size() == 4);
    REQUIRE(measures[0].beats[3].notes.empty());
}

TEST_CASE("sequencer: three notes in 3/4 leave no rest; five notes in 4/4 become eighths (section 1)")
{
    const Voicing shell{{6, 8}, {4, 9}, {3, 9}};
    const auto threeFour = slotMeasures({shell}, model::TimeSignature{3, 4}, PlaybackStyle::Arp);
    REQUIRE(threeFour[0].beats.size() == 3);
    REQUIRE(threeFour[0].beats[2].notes.size() == 1);

    const Voicing five{{6, 8}, {4, 9}, {3, 9}, {2, 8}, {1, 10}};
    const auto fourFour = slotMeasures({five}, model::TimeSignature{4, 4}, PlaybackStyle::Arp);
    REQUIRE(fourFour[0].beats.size() == 8);
    REQUIRE(fourFour[0].beats[0].dur == model::Dur::Eighth);
    REQUIRE(fourFour[0].beats[4].notes.size() == 1);
    REQUIRE(fourFour[0].beats[5].notes.empty());
}

TEST_CASE("sequencer: scale run climbs the form then descends, one note per beat, last measure padded")
{
    const theory::Tuning tuning = theory::Tuning::standardSix();
    Voicing form;
    for (const theory::ScaleNote &n : theory::diagonalMajorForm(0, theory::RootString::Five, tuning)) {
        form.push_back(n.position);
    }
    const auto measures = scaleRunMeasures(form, model::TimeSignature{4, 4});
    REQUIRE(measures.size() == 11); // 21 up + 20 down = 41 beats -> 10 full measures + 1
    const auto events = noteEvents(scoreOf(measures, model::TimeSignature{4, 4}));
    REQUIRE(events.size() == 41);
    for (std::size_t i = 1; i < 21; ++i) {
        REQUIRE(events[i].pitch > events[i - 1].pitch);
    }
    for (std::size_t i = 21; i < 41; ++i) {
        REQUIRE(events[i].pitch < events[i - 1].pitch);
    }
    REQUIRE(events.back().pitch == events.front().pitch);
    REQUIRE(measures.back().beats[3].notes.empty());
}
