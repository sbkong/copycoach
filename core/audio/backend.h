// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/audioblock.h"
#include "core/error.h"

#include <cstdint>
#include <string>
#include <vector>

// Device backend seam (audio-engine.md section 6, PLAN 4-1). v1 implementation = PortAudio (app side); the
// NullBackend shares the contract tests. ASIO-only extras (control panel, channel selectors) come with that
// backend, not here.
namespace cc::audio
{

struct DeviceInfo {
    std::string hostApi;
    std::string name;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
    double defaultSampleRate = 0.0;
};

struct DeviceConfig {
    std::string hostApi; // "asio" | "wasapi-exclusive" | "wasapi-shared" (setting audio.backend values)
    std::string inputDevice; // empty = none
    std::string outputDevice; // empty = default
    double sampleRate = 48000.0;
    int inputChannels = 0;
    int outputChannels = 2;
    int bufferFrames = 0; // 0 = let the host choose (paFramesPerBufferUnspecified)
};

struct StreamInfo {
    double sampleRate = 0.0;
    double inputLatencySeconds = 0.0;
    double outputLatencySeconds = 0.0;
    int bufferFrames = 0;
};

/// Bits the backend passes with every callback (PortAudio paInputOverflow / paOutputUnderflow).
constexpr std::uint32_t statusInputOverflow = 1U;
constexpr std::uint32_t statusOutputUnderflow = 2U;

/// What the backend calls from its audio thread. `input` may have zero channels.
class IAudioProcessor
{
public:
    virtual ~IAudioProcessor() = default;
    IAudioProcessor() = default;
    IAudioProcessor(const IAudioProcessor &) = delete;
    IAudioProcessor &operator=(const IAudioProcessor &) = delete;
    IAudioProcessor(IAudioProcessor &&) = delete;
    IAudioProcessor &operator=(IAudioProcessor &&) = delete;

    virtual void process(const rt::AudioBlock &input, rt::AudioBlock &output, std::uint32_t statusFlags) = 0;
};

class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;
    IAudioBackend() = default;
    IAudioBackend(const IAudioBackend &) = delete;
    IAudioBackend &operator=(const IAudioBackend &) = delete;
    IAudioBackend(IAudioBackend &&) = delete;
    IAudioBackend &operator=(IAudioBackend &&) = delete;

    [[nodiscard]] virtual Result<std::vector<DeviceInfo>> enumerate() = 0;
    /// Opens one stream; DeviceBusy when one is already open, DeviceOpenFailed otherwise.
    [[nodiscard]] virtual Result<StreamInfo> open(const DeviceConfig &config, IAudioProcessor &processor) = 0;
    [[nodiscard]] virtual Result<void> start() = 0;
    [[nodiscard]] virtual Result<void> stop() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
};

} // namespace cc::audio
