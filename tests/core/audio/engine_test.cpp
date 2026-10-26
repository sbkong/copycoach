// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Offline engine rendering (audio-engine.md section 10): count-in length, loop split, tempo change, mute ramp,
// metronome beat positions, determinism, no allocation on the callback.
#include "core/audio/engine.h"
#include "core/audio/nullbackend.h"
#include "core/audio/rt/allocguard.h"
#include "core/model/timemap.h"
#include "core/routine/sequencer.h"
#include "core/theory/shellvoicing.h"
#include "testsynth.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace cc;
using namespace cc::audio;

namespace
{

constexpr double rate = 48000.0;
constexpr int pumpFrames = 256;
constexpr std::int64_t quarter = 960;
constexpr std::int64_t measure = 4 * quarter;
constexpr float accentLevel = 0.8F; // below the soft clip knee so the impulse comes out unchanged
constexpr float beatLevel = 0.5F;
constexpr float subLevel = 0.25F;

struct Rig {
    test::TestSynth synth;
    Engine engine{rate, synth};
    NullBackend backend;
    std::vector<float> left;
    std::vector<Event> events;

    Rig()
    {
        DeviceConfig config;
        config.bufferFrames = pumpFrames;
        REQUIRE(backend.open(config, engine));
        REQUIRE(backend.start());
    }

    void post(CommandType type, std::int64_t a = 0, std::int64_t b = 0, double value = 0.0, const void *res = nullptr)
    {
        REQUIRE(engine.post(Command{type, a, b, value, res}));
    }

    /// Metronome with single-sample clicks panned hard left, so the left channel shows click positions.
    void useImpulseMetronome(const MetronomeSet &set)
    {
        post(CommandType::SwapMetronomeSet, 0, 0, 0.0, &set);
        post(CommandType::SetMetronomePan, 0, 0, -1.0);
    }

    /// Renders `frames` samples in callbacks of pumpFrames, collecting the left channel and the events.
    void render(int frames)
    {
        for (int done = 0; done < frames; done += pumpFrames) {
            REQUIRE(backend.pump(std::min(pumpFrames, frames - done)));
            const std::span<const float> out = backend.output(0);
            left.insert(left.end(), out.begin(), out.end());
            Event event;
            while (engine.pollEvent(event)) {
                events.push_back(event);
            }
        }
    }

    [[nodiscard]] std::vector<std::int64_t> eventValues(EventType type) const
    {
        std::vector<std::int64_t> out;
        for (const Event &e : events) {
            if (e.type == type) {
                out.push_back(e.a);
            }
        }
        return out;
    }

    [[nodiscard]] std::vector<std::int64_t> impulsesAt(float level) const
    {
        std::vector<std::int64_t> out;
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (std::abs(left[i] - level) < 1e-4F) {
                out.push_back(static_cast<std::int64_t>(i));
            }
        }
        return out;
    }
};

NoteSequence shellSequence(model::Score &score)
{
    const theory::Tuning tuning = theory::Tuning::standardSix();
    std::vector<routine::Voicing> slots;
    for (const theory::ShellSlot &slot : theory::shellLoop(0, theory::LoopOrder::Circle5, tuning)) {
        slots.push_back(theory::shellNotes(slot, tuning));
    }
    model::IdGenerator ids = model::IdGenerator::seeded(7);
    model::ScoreSpec spec;
    spec.tempoBpm = 120.0;
    spec.measures = routine::slotMeasures(slots, model::TimeSignature{4, 4}, routine::PlaybackStyle::Chord);
    score = model::buildScore(spec, ids);
    return makeNoteSequence(routine::noteEvents(score), score);
}

MetronomeSet impulseSet()
{
    MetronomeSet set;
    set.accent = {accentLevel};
    set.beat = {beatLevel};
    set.sub = {subLevel};
    return set;
}

} // namespace

// 120 BPM at 48 kHz: 25 samples per tick, a quarter = 24000 samples, a measure = 96000.

TEST_CASE("engine: count-in lasts exactly the count-in ticks and the metronome rings during it")
{
    Rig rig;
    const MetronomeSet set = impulseSet();
    rig.useImpulseMetronome(set);
    rig.post(CommandType::SetTempo, 0, 0, 120.0);
    rig.post(CommandType::SetMetronome, 0, 0); // off: the count-in must still ring (decision 2026-10-26)
    rig.post(CommandType::Play, measure);
    rig.render(96000 + 24000);

    REQUIRE(rig.eventValues(EventType::CountInDone) == std::vector<std::int64_t>{96000});
    REQUIRE(rig.engine.snapshot().state == EngineState::Playing);
    REQUIRE(rig.impulsesAt(accentLevel) == std::vector<std::int64_t>{0});
    REQUIRE(rig.impulsesAt(beatLevel) == std::vector<std::int64_t>{24000, 48000, 72000}); // none after the count-in
}

TEST_CASE("engine: metronome beats land where TimeMap puts them, and the beat mask silences beats")
{
    Rig rig;
    const MetronomeSet set = impulseSet();
    rig.useImpulseMetronome(set);
    rig.post(CommandType::SetTempo, 0, 0, 100.0); // 30 samples per tick
    rig.post(CommandType::SetMetronome, 0, 1);
    rig.post(CommandType::SetSubdivision, 2);
    rig.post(CommandType::SetBeatMask, 0b1011); // beat 3 off
    rig.post(CommandType::Play, 0);
    rig.render(2 * 3840 * 30); // exactly two measures

    const model::TimeMap map(rate, {model::TempoSegment{0, 0, 100.0}});
    REQUIRE(rig.impulsesAt(accentLevel) == std::vector<std::int64_t>{map.tickToSample(0), map.tickToSample(measure)});
    REQUIRE(rig.impulsesAt(beatLevel)
            == std::vector<std::int64_t>{map.tickToSample(quarter),
                                         map.tickToSample(3 * quarter),
                                         map.tickToSample(measure + quarter),
                                         map.tickToSample(measure + (3 * quarter))});
    const auto subs = rig.impulsesAt(subLevel);
    REQUIRE(subs.size() == 8);
    REQUIRE(subs[0] == map.tickToSample(quarter / 2));
    REQUIRE(subs[2] == map.tickToSample((2 * quarter) + (quarter / 2))); // subdivision of the masked beat still plays
}

TEST_CASE("engine: loop wraps inside a callback block without losing or repeating a sample")
{
    Rig rig;
    model::Score score;
    const NoteSequence sequence = shellSequence(score);
    rig.post(CommandType::SwapSequence, 0, 0, 0.0, &sequence);
    rig.post(CommandType::SetTempo, 0, 0, 120.0);
    rig.post(CommandType::SetLoop, 0, measure + 37); // 96925 samples: not a multiple of 256 nor 64
    rig.post(CommandType::Play, 0);
    rig.render(96925 + 1000);

    REQUIRE(rig.eventValues(EventType::LoopWrapped) == std::vector<std::int64_t>{96925});
    REQUIRE(rig.engine.snapshot().positionSamples == 96925 + 1000);
    REQUIRE(rig.engine.snapshot().positionTicks == 40); // 1000 samples after the wrap = 40 ticks
}

TEST_CASE("engine: slot changes follow the measures and the sequence restarts after the wrap")
{
    Rig rig;
    model::Score score;
    const NoteSequence sequence = shellSequence(score);
    rig.post(CommandType::SwapSequence, 0, 0, 0.0, &sequence);
    rig.post(CommandType::SetTempo, 0, 0, 120.0);
    rig.post(CommandType::SetLoop, 0, 2 * measure);
    rig.post(CommandType::Play, 0);
    rig.render((2 * 2 * 96000) - pumpFrames); // one callback short of the second wrap

    REQUIRE(rig.eventValues(EventType::SlotChanged) == std::vector<std::int64_t>{0, 1, 0, 1});
    REQUIRE(rig.synth.noteOns == 4 * 3); // 3 shell notes per slot, four slots played
    REQUIRE(rig.left[100] == 3 * test::TestSynth::level);
}

TEST_CASE("engine: tempo change moves the next measure boundary the way TimeMap segments do")
{
    Rig rig;
    const MetronomeSet set = impulseSet();
    rig.useImpulseMetronome(set);
    rig.post(CommandType::SetTempo, 0, 0, 120.0);
    rig.post(CommandType::SetMetronome, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.render(pumpFrames * 50); // 12800 samples = tick 512
    rig.post(CommandType::SetTempo, 0, 0, 60.0); // 50 samples per tick from here
    rig.render(pumpFrames * 800);

    const std::int64_t secondDownbeat = 12800 + ((measure - 512) * 50);
    REQUIRE(rig.impulsesAt(accentLevel) == std::vector<std::int64_t>{0, secondDownbeat});
}

TEST_CASE("engine: mute ramps over one 64-frame block, unmute ramps back")
{
    Rig rig;
    rig.post(CommandType::NoteOn, guitarChannel, 60, 1.0);
    rig.render(pumpFrames);
    REQUIRE(rig.left.back() == test::TestSynth::level);
    rig.post(CommandType::SetMute, static_cast<std::int64_t>(Source::Synth), 1);
    rig.render(pumpFrames);
    const std::span<const float> block = std::span<const float>(rig.left).subspan(pumpFrames, rt::blockFrames);
    REQUIRE(block[0] < test::TestSynth::level);
    REQUIRE(block[0] > 0.0F);
    REQUIRE(block[31] > block[32]);
    REQUIRE(std::abs(block[63]) < 1e-6F);
    REQUIRE(rig.left.back() == 0.0F);
    rig.post(CommandType::SetMute, static_cast<std::int64_t>(Source::Synth), 0);
    rig.render(pumpFrames);
    REQUIRE(rig.left.back() == test::TestSynth::level);
}

TEST_CASE("engine: same commands render the same samples, and the callback allocates nothing")
{
    const auto run = [] {
        Rig rig;
        model::Score score;
        const NoteSequence sequence = shellSequence(score);
        const MetronomeSet set = impulseSet();
        rig.post(CommandType::SwapSequence, 0, 0, 0.0, &sequence);
        rig.useImpulseMetronome(set);
        rig.post(CommandType::SetMetronome, 0, 1);
        rig.post(CommandType::SetLoop, 0, measure);
        rig.post(CommandType::Play, measure);
        rt::resetForbiddenAllocationCount();
        rig.render(pumpFrames * 300);
        REQUIRE(rt::forbiddenAllocationCount() == 0);
        return rig.left;
    };
    REQUIRE(run() == run());
}

TEST_CASE("engine: stop keeps the position and reports it; seek while stopped moves it")
{
    Rig rig;
    rig.post(CommandType::SetTempo, 0, 0, 120.0);
    rig.post(CommandType::Play, 0);
    rig.render(pumpFrames * 10);
    rig.post(CommandType::Stop);
    rig.render(pumpFrames);
    REQUIRE(rig.eventValues(EventType::Stopped) == std::vector<std::int64_t>{std::int64_t{pumpFrames} * 10});
    REQUIRE(rig.engine.snapshot().state == EngineState::Stopped);
    REQUIRE(rig.engine.snapshot().positionSamples == std::int64_t{pumpFrames} * 10);
    rig.post(CommandType::Seek, measure);
    rig.render(pumpFrames);
    REQUIRE(rig.engine.snapshot().positionTicks == measure);
}
