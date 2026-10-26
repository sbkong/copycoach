// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Backend contract (audio-engine.md section 10): open -> start -> callback -> stop -> close, errors as Result.
#include "core/audio/nullbackend.h"

#include <catch2/catch_test_macros.hpp>

using namespace cc;
using namespace cc::audio;

namespace
{

class CountingProcessor final : public IAudioProcessor
{
public:
    void process(const rt::AudioBlock &input, rt::AudioBlock &output, std::uint32_t statusFlags) override
    {
        ++calls;
        lastFrames = output.frames;
        inputChannels = static_cast<int>(input.channels.size());
        lastFlags = statusFlags;
        for (std::span<float> ch : output.channels) {
            for (float &s : ch) {
                s = 0.5F;
            }
        }
    }

    int calls = 0;
    int lastFrames = 0;
    int inputChannels = -1;
    std::uint32_t lastFlags = 0;
};

} // namespace

TEST_CASE("null backend: lifecycle order and errors")
{
    NullBackend backend;
    CountingProcessor processor;
    REQUIRE_FALSE(backend.isOpen());
    REQUIRE_FALSE(backend.start()); // start before open fails
    REQUIRE(backend.start().error().code == ErrorCode::DeviceOpenFailed);

    DeviceConfig config;
    config.inputChannels = 1;
    config.bufferFrames = 128;
    const auto info = backend.open(config, processor);
    REQUIRE(info);
    REQUIRE(info->sampleRate == 48000.0);
    REQUIRE(info->bufferFrames == 128);
    REQUIRE(backend.open(config, processor).error().code == ErrorCode::DeviceBusy);

    REQUIRE_FALSE(backend.pump(64)); // not running yet
    REQUIRE(backend.start());
    REQUIRE(backend.isRunning());
    REQUIRE(backend.pump(64, statusOutputUnderflow));
    REQUIRE(processor.calls == 1);
    REQUIRE(processor.lastFrames == 64);
    REQUIRE(processor.inputChannels == 1);
    REQUIRE(processor.lastFlags == statusOutputUnderflow);
    REQUIRE(backend.output(0).size() == 64);
    REQUIRE(backend.output(1)[63] == 0.5F);

    REQUIRE(backend.pump(1000)); // clamped to the buffer
    REQUIRE(processor.lastFrames == 128);

    REQUIRE(backend.stop());
    REQUIRE_FALSE(backend.pump(64));
    backend.close();
    REQUIRE_FALSE(backend.isOpen());
}

TEST_CASE("null backend: rejects unsupported configurations")
{
    NullBackend backend;
    CountingProcessor processor;
    DeviceConfig config;
    config.outputChannels = 6;
    REQUIRE(backend.open(config, processor).error().code == ErrorCode::DeviceOpenFailed);
    REQUIRE_FALSE(backend.isOpen());
}
