// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// File playback through the render worker and the chunk ring (audio-engine.md section 15.7): sample-exact
// pass-through, ticks following sync points, loop wrap on the source timeline, seek flush, end of file,
// underrun, mono + resampled sources, stem mix, the worker thread, no allocation on the callback.
#include "core/audio/engine.h"
#include "core/audio/nullbackend.h"
#include "core/audio/renderworker.h"
#include "core/audio/rt/allocguard.h"
#include "core/media/audiofile.h"
#include "core/model/scorebuilder.h"
#include "core/model/timemap.h"
#include "testsynth.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <thread>
#include <vector>

using namespace cc;
using namespace cc::audio;

namespace
{

constexpr double rate = 48000.0;
constexpr int pumpFrames = 256;
constexpr std::int64_t second = 48000;

/// The test signal, recomputed rather than stored: left = sine, right = a 100 Hz saw, both well under the clip knee.
float expected(std::int64_t frame, std::size_t channel, double fileRate)
{
    if (channel == 0) {
        return static_cast<float>(0.5
                                  * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(frame) / fileRate));
    }
    const auto period = static_cast<std::int64_t>(fileRate / 100.0);
    return 0.5F * static_cast<float>(frame % period) / static_cast<float>(period);
}

std::filesystem::path testWav(const char *name, double fileRate, int channels, double seconds)
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "render";
    std::filesystem::create_directories(dir);
    const auto path = dir / (std::string(name) + ".wav");
    const auto frames = static_cast<std::int64_t>(seconds * fileRate);
    const auto channelCount = static_cast<std::size_t>(channels);
    std::vector<float> data(static_cast<std::size_t>(frames) * channelCount);
    for (std::int64_t f = 0; f < frames; ++f) {
        for (std::size_t c = 0; c < channelCount; ++c) {
            data[(static_cast<std::size_t>(f) * static_cast<std::size_t>(channels)) + c] = expected(f, c, fileRate);
        }
    }
    media::AudioFileWriter writer;
    REQUIRE(writer.open(path, fileRate, channels, media::AudioFileFormat::WavFloat));
    REQUIRE(writer.write(data));
    REQUIRE(writer.close());
    return path;
}

struct FileRig {
    test::TestSynth synth;
    Engine engine{rate, synth};
    NullBackend backend;
    RenderWorker worker{engine.fileRing(), rate};
    std::vector<float> left;
    std::vector<float> right;
    std::vector<Event> events;

    FileRig()
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

    void pumpOnce(int frames)
    {
        REQUIRE(backend.pump(frames));
        const std::span<const float> l = backend.output(0);
        const std::span<const float> r = backend.output(1);
        left.insert(left.end(), l.begin(), l.end());
        right.insert(right.end(), r.begin(), r.end());
        Event event;
        while (engine.pollEvent(event)) {
            events.push_back(event);
        }
    }

    /// Deterministic: the worker fills before every callback, as a worker that always keeps up would.
    void render(std::int64_t frames)
    {
        for (std::int64_t done = 0; done < frames; done += pumpFrames) {
            worker.fill();
            pumpOnce(static_cast<int>(std::min<std::int64_t>(pumpFrames, frames - done)));
        }
    }

    /// Output frames from `from` that differ from the file at `fileFrom + k` (exact float compare).
    [[nodiscard]] std::int64_t mismatches(std::int64_t from,
                                          std::int64_t fileFrom,
                                          std::int64_t count,
                                          float scale = 1.0F,
                                          float tolerance = 0.0F) const
    {
        std::int64_t bad = 0;
        for (std::int64_t k = 0; k < count; ++k) {
            const auto i = static_cast<std::size_t>(from + k);
            const float l = expected(fileFrom + k, 0, rate) * scale;
            const float r = expected(fileFrom + k, 1, rate) * scale;
            if (std::abs(left[i] - l) > tolerance || std::abs(right[i] - r) > tolerance) {
                ++bad;
            }
        }
        return bad;
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
};

/// 60 bpm score pinned so that bar 1 starts at 1 s and bar 2 at 3 s (120 bpm effective between the pins).
model::TimeMap pinnedMap(model::Score &score)
{
    model::IdGenerator ids = model::IdGenerator::seeded(11);
    model::ScoreSpec spec;
    spec.tempoBpm = 60.0;
    spec.measures.assign(8, model::MeasureSpec{});
    score = model::buildScore(spec, ids);
    const std::vector<model::SyncPoint> pins{{0, 1.0}, {3840, 3.0}};
    return model::TimeMap::fromSyncPoints(score, pins, rate);
}

} // namespace

TEST_CASE("file playback: samples pass through exactly and the tick cursor follows the sync points")
{
    FileRig rig;
    const auto path = testWav("pass", rate, 2, 6.0);
    REQUIRE(rig.worker.load({path}));
    REQUIRE(rig.worker.lengthFrames() == 6 * second);
    model::Score score;
    const model::TimeMap map = pinnedMap(score);
    rig.post(CommandType::SwapTimeMap, 0, 0, 0.0, &map);
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rt::resetForbiddenAllocationCount();

    rig.render(2 * second);
    EngineSnapshot s = rig.engine.snapshot();
    REQUIRE(s.sourceFrame == 2 * second);
    REQUIRE(s.positionTicks == 1920); // 2 s = halfway between the pins at 120 bpm effective
    rig.render(2 * second);
    s = rig.engine.snapshot();
    REQUIRE(s.sourceFrame == 4 * second);
    REQUIRE(s.positionTicks == 3840 + 960); // after the last pin the score's 60 bpm resumes
    REQUIRE(s.fileUnderruns == 0);
    REQUIRE(rig.mismatches(0, 0, 4 * second) == 0);
    REQUIRE(rig.eventValues(EventType::FileUnderrun).empty());
    REQUIRE(rt::forbiddenAllocationCount() == 0);
}

TEST_CASE("file playback: the loop wraps on the source timeline without a gap and reports LoopWrapped")
{
    FileRig rig;
    const auto path = testWav("loop", rate, 2, 4.0);
    REQUIRE(rig.worker.load({path}));
    rig.worker.setLoop(second, 2 * second);
    rig.worker.seek(second);
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.render(3 * second);
    for (std::int64_t lap = 0; lap < 3; ++lap) {
        REQUIRE(rig.mismatches(lap * second, second, second) == 0);
    }
    REQUIRE(rig.eventValues(EventType::LoopWrapped) == std::vector<std::int64_t>{second, 2 * second});
}

TEST_CASE("file playback: a seek drops what the ring already holds and continues from the new frame")
{
    FileRig rig;
    const auto path = testWav("seek", rate, 2, 8.0);
    REQUIRE(rig.worker.load({path}));
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.render(second);
    rig.worker.seek(5 * second);
    rig.render(second);
    REQUIRE(rig.mismatches(0, 0, second) == 0);
    REQUIRE(rig.mismatches(second, 5 * second, second) == 0);
    REQUIRE(rig.engine.snapshot().sourceFrame == 6 * second);
}

TEST_CASE("file playback: the end of the sources stops the transport and leaves silence")
{
    FileRig rig;
    const auto path = testWav("end", rate, 2, 1.0);
    REQUIRE(rig.worker.load({path}));
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.render(second + (second / 2));
    REQUIRE(rig.worker.ended());
    REQUIRE(rig.eventValues(EventType::FileEnded) == std::vector<std::int64_t>{second});
    REQUIRE(rig.eventValues(EventType::Stopped) == std::vector<std::int64_t>{second});
    REQUIRE(rig.engine.snapshot().state == EngineState::Stopped);
    REQUIRE(rig.mismatches(0, 0, second) == 0);
    REQUIRE(
        std::ranges::all_of(std::span<const float>(rig.left).subspan(static_cast<std::size_t>(second)), [](float v) {
            return v == 0.0F;
        }));
}

TEST_CASE("file playback: an empty ring is an underrun with silence, and the source position waits")
{
    FileRig rig;
    const auto path = testWav("under", rate, 2, 2.0);
    REQUIRE(rig.worker.load({path}));
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.pumpOnce(pumpFrames); // nobody filled the ring
    REQUIRE(rig.engine.snapshot().fileUnderruns >= 1);
    REQUIRE(rig.engine.snapshot().sourceFrame == -1);
    REQUIRE(std::ranges::all_of(rig.left, [](float v) {
        return v == 0.0F;
    }));
    rig.render(second);
    REQUIRE(rig.mismatches(pumpFrames, 0, second) == 0); // playback starts at frame 0 once chunks arrive
}

TEST_CASE("file playback: a mono 44.1 k file is resampled to both channels and runs the right length")
{
    FileRig rig;
    const auto path = testWav("mono", 44100.0, 1, 1.0);
    REQUIRE(rig.worker.load({path}));
    REQUIRE(std::abs(rig.worker.lengthFrames() - second) <= 1);
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.render(second + (second / 4));
    REQUIRE(rig.worker.ended());
    const std::vector<std::int64_t> ended = rig.eventValues(EventType::FileEnded);
    REQUIRE(ended.size() == 1);
    REQUIRE(std::abs(ended[0] - second) <= 128);
    float peak = 0.0F;
    for (std::size_t i = 1000; i < static_cast<std::size_t>(second) - 1000; ++i) {
        REQUIRE(rig.left[i] == rig.right[i]);
        peak = std::max(peak, std::abs(rig.left[i]));
    }
    REQUIRE(peak > 0.45F);
}

TEST_CASE("file playback: stems mix with gain, mute and solo resolved before the ring")
{
    FileRig rig;
    const auto path = testWav("stems", rate, 2, 2.0);
    REQUIRE(rig.worker.load({path, path}));
    REQUIRE(rig.worker.sourceCount() == 2);
    rig.worker.setFillTarget(std::int64_t{2}
                             * pumpFrames); // a short ring: mix changes land within 512 frames (section 15.2)
    rig.worker.setGain(1, 0.5F);
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.render(second / 2);
    REQUIRE(rig.mismatches(0, 0, second / 2, 1.5F, 1e-6F) == 0); // x + 0.5x is not bit-identical to 1.5x
    constexpr std::int64_t latency = std::int64_t{2} * pumpFrames; // what the ring already held when the change arrived
    rig.worker.setMute(0, true);
    rig.render(second / 2);
    REQUIRE(rig.mismatches((second / 2) + latency, (second / 2) + latency, (second / 2) - latency, 0.5F) == 0);
    rig.worker.setMute(0, false);
    rig.worker.setSolo(0, true);
    rig.render(second / 2);
    REQUIRE(rig.mismatches(second + latency, second + latency, (second / 2) - latency, 1.0F) == 0);
}

TEST_CASE("file playback: the worker thread keeps the ring fed")
{
    FileRig rig;
    const auto path = testWav("thread", rate, 2, 3.0);
    REQUIRE(rig.worker.load({path}));
    rig.post(CommandType::SetFileMode, 0, 1);
    rig.post(CommandType::Play, 0);
    rig.worker.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::int64_t done = 0;
    while (done < second && std::chrono::steady_clock::now() < deadline) {
        if (rig.engine.fileRing().queue.size() < 8) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        rig.pumpOnce(pumpFrames);
        done += pumpFrames;
    }
    rig.worker.stop();
    REQUIRE(done >= second);
    REQUIRE(rig.mismatches(0, 0, second) == 0);
    REQUIRE(rig.engine.snapshot().fileUnderruns == 0);
}
