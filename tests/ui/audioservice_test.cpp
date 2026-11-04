// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// AudioService copy API (audio-engine.md section 15.8, piece 17d): sources, timing map, mode switch, seek, loop,
// speed / pitch clamps, end of the recording, device reopen. Over RDP the service runs on the silent fallback,
// pumped in real time by its timer, so these checks are about wiring and ranges; sample exactness and the tick
// arithmetic are the core worker tests' job.
#include "core/audio/renderworker.h"
#include "core/media/audiofile.h"
#include "core/model/scorebuilder.h"
#include "core/model/timemap.h"
#include "ui/audio/audioservice.h"
#include "ui/shell/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QTest>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::ui;

namespace
{

constexpr double fileRate = 48000.0;
constexpr std::int64_t barTicks = 3840; // 4/4 at 960 ticks per quarter
constexpr int settleMs = 80; // a few polls (33 ms) so a posted Stop / Seek shows in the snapshot

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "audioservice";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    std::int64_t frame = -1;
    std::int64_t ticks = -1;
    bool ended = false;

    Fixture()
    {
        std::filesystem::create_directories(userDir);
    }

    void watch(AudioService &audio)
    {
        QObject::connect(&audio, &AudioService::sourceFrameChanged, [this](std::int64_t f) {
            frame = f;
        });
        QObject::connect(&audio, &AudioService::positionChanged, [this](std::int64_t t) {
            ticks = t;
        });
        QObject::connect(&audio, &AudioService::sourcesEnded, [this] {
            ended = true;
        });
    }

    [[nodiscard]] std::filesystem::path sineWav(const char *name, double seconds) const
    {
        const auto path = userDir / (std::string(name) + ".wav");
        const auto frames = static_cast<std::size_t>(seconds * fileRate);
        std::vector<float> data(frames * 2);
        for (std::size_t i = 0; i < frames; ++i) {
            const auto v =
                static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / fileRate));
            data[2 * i] = v;
            data[(2 * i) + 1] = v;
        }
        media::AudioFileWriter writer;
        REQUIRE(writer.open(path, fileRate, 2, media::AudioFileFormat::WavFloat));
        REQUIRE(writer.write(data));
        REQUIRE(writer.close());
        return path;
    }
};

/// 60 bpm, 8 bars of 4/4.
model::Score slowScore()
{
    model::IdGenerator ids = model::IdGenerator::seeded(17);
    model::ScoreSpec spec;
    spec.tempoBpm = 60.0;
    spec.measures.assign(8, model::MeasureSpec{});
    return model::buildScore(spec, ids);
}

/// Bar 0 starts at 1 s of the recording and bar 1 at 3 s: 120 bpm effective between the pins.
std::vector<model::SyncPoint> pins()
{
    return {{0, 1.0}, {barTicks, 3.0}};
}

} // namespace

TEST_CASE("audio service copy: sources, timing and play drive the source frame and the tick cursor together")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    f.watch(audio);
    REQUIRE(audio.sourceCount() == 0);
    REQUIRE_FALSE(audio.copyMode());

    REQUIRE(audio.loadSources({f.sineWav("play", 3.0)}));
    REQUIRE(audio.sourceCount() == 1);
    REQUIRE(audio.sourceLengthFrames() > 0);
    audio.setCopyTiming(slowScore(), pins());
    REQUIRE(audio.copyTimeMap() != nullptr);
    audio.setCopyMode(true);
    REQUIRE(audio.copyMode());

    audio.play(false);
    QTest::qWait(600);
    REQUIRE(audio.isPlaying());
    REQUIRE(f.frame > 0);
    REQUIRE(audio.sourceFrame() == f.frame);
    REQUIRE(std::abs(audio.copyTimeMap()->sampleToTick(f.frame) - f.ticks) <= 2); // one tick of sync tolerance

    audio.stop();
    QTest::qWait(settleMs);
    REQUIRE_FALSE(audio.isPlaying());
    audio.setCopyMode(false);
    REQUIRE_FALSE(audio.copyMode());
}

TEST_CASE("audio service copy: a seek moves the recording through the map and the loop holds the range")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    f.watch(audio);
    REQUIRE(audio.loadSources({f.sineWav("seek", 4.0)}));
    audio.setCopyTiming(slowScore(), pins());
    audio.setCopyMode(true);
    const std::int64_t from = barTicks / 2; // 2.0 s of the recording
    const std::int64_t to = (barTicks * 3) / 4; // 2.5 s
    audio.setCopyLoop(from, to);
    audio.seekToTick(from);
    QTest::qWait(settleMs);
    REQUIRE(f.ticks == from); // stopped: the cursor sits on the seek target

    audio.play(false);
    QTest::qWait(900); // nearly two laps of the half-second loop
    REQUIRE(audio.isPlaying());
    const model::TimeMap &map = *audio.copyTimeMap();
    REQUIRE(f.frame >= map.tickToSample(from));
    REQUIRE(f.frame < map.tickToSample(to) + 256);
    REQUIRE(f.ticks >= from);
    REQUIRE(f.ticks <= to + 4);
    audio.stop();
}

TEST_CASE("audio service copy: speed and pitch clamp to the worker range and routine playback survives the switch")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    f.watch(audio);
    REQUIRE(audio.loadSources({f.sineWav("speed", 2.0)}));
    audio.setCopyTiming(slowScore(), pins());
    audio.setCopyMode(true);

    audio.setSpeed(3.0);
    REQUIRE(audio.speed() == audio::RenderWorker::maxSpeed);
    audio.setSpeed(0.1);
    REQUIRE(audio.speed() == audio::RenderWorker::minSpeed);
    audio.setPitch(-20.0);
    REQUIRE(audio.pitch() == -audio::RenderWorker::maxPitchSemitones);
    audio.setStemGain(7, 0.5F); // out of range: ignored
    audio.setStemMute(0, true);
    audio.setStemMute(0, false);

    audio.setSpeed(0.5);
    audio.seekToTick(0);
    audio.play(false);
    QTest::qWait(500);
    REQUIRE(f.frame > 0);
    REQUIRE(std::abs(audio.copyTimeMap()->sampleToTick(f.frame) - f.ticks) <= 2); // ticks follow the frame at 50 % too

    audio.setCopyMode(false);
    QTest::qWait(settleMs);
    REQUIRE_FALSE(audio.isPlaying());
    audio.setTempo(120.0);
    audio.seekToTick(0);
    audio.play(false);
    QTest::qWait(300);
    REQUIRE(audio.isPlaying());
    REQUIRE(f.ticks > 0);
    REQUIRE(f.ticks < 2 * barTicks); // the routine clock, well under a second of 120 bpm
    audio.stop();
}

TEST_CASE("audio service copy: the end of the recording stops and reports, a device reopen keeps the sources")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    f.watch(audio);
    REQUIRE(audio.loadSources({f.sineWav("end", 0.5)}));
    audio.setCopyTiming(slowScore(), {}); // no pins: the score tempo from 0 s
    audio.setCopyMode(true);
    audio.seekToTick(0);
    audio.play(false);
    QTest::qWait(1200);
    REQUIRE(f.ended);
    REQUIRE_FALSE(audio.isPlaying());

    static_cast<void>(audio.reopenDevice()); // fails over RDP (silent fallback again): the state must survive
    REQUIRE(audio.copyMode());
    REQUIRE(audio.sourceCount() == 1);
    REQUIRE(audio.sourceLengthFrames() > 0);
    f.frame = -1;
    audio.seekToTick(0);
    audio.play(false);
    QTest::qWait(300);
    REQUIRE(f.frame > 0);

    audio.unloadSources();
    REQUIRE(audio.sourceCount() == 0);
    REQUIRE(audio.sourceLengthFrames() == 0);
}
