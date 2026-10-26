// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/backend.h"

#include <span>
#include <vector>

// Backend with no device: the test (or an offline renderer) pumps the processor itself (audio-engine.md
// section 10). Output of the last pump stays readable.
namespace cc::audio
{

class NullBackend final : public IAudioBackend
{
public:
    Result<std::vector<DeviceInfo>> enumerate() override;
    Result<StreamInfo> open(const DeviceConfig &config, IAudioProcessor &processor) override;
    Result<void> start() override;
    Result<void> stop() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] bool isRunning() const override;

    /// Runs one callback of `frames` frames (at most the configured buffer) with silent input.
    /// Returns false when the stream is not running.
    bool pump(int frames, std::uint32_t statusFlags = 0);

    /// Output of the last pump, channel `index`.
    [[nodiscard]] std::span<const float> output(std::size_t index) const;
    [[nodiscard]] const DeviceConfig &config() const;

private:
    IAudioProcessor *m_processor = nullptr;
    DeviceConfig m_config;
    bool m_open = false;
    bool m_running = false;
    int m_lastFrames = 0;
    std::vector<std::vector<float>> m_input;
    std::vector<std::vector<float>> m_output;
    std::vector<std::span<float>> m_inputSpans;
    std::vector<std::span<float>> m_outputSpans;
};

} // namespace cc::audio
