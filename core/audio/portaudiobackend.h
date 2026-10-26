// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/backend.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

// PortAudio v19 backend (audio-engine.md section 6, PLAN 4-1): ASIO or WASAPI host, planar float32 streams,
// host-chosen buffer size unless the config asks for one. ASIO channel selectors, control panel and the
// WASAPI exclusive flags come with the device preferences page (TODO.md).
namespace cc::audio
{

class PortAudioBackend final : public IAudioBackend
{
public:
    PortAudioBackend();
    ~PortAudioBackend() override;
    PortAudioBackend(const PortAudioBackend &) = delete;
    PortAudioBackend &operator=(const PortAudioBackend &) = delete;
    PortAudioBackend(PortAudioBackend &&) = delete;
    PortAudioBackend &operator=(PortAudioBackend &&) = delete;

    Result<std::vector<DeviceInfo>> enumerate() override;
    Result<StreamInfo> open(const DeviceConfig &config, IAudioProcessor &processor) override;
    Result<void> start() override;
    Result<void> stop() override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] bool isRunning() const override;

    /// Opens the ASIO driver's own settings window (PaAsio_ShowControlPanel); `window` is the parent HWND as an
    /// integer (QWidget::winId), converted to the pointer PortAudio wants here, in the one adapter that knows.
    [[nodiscard]] Result<void> showAsioControlPanel(const std::string &driverName, std::uintptr_t window) const;

    /// Called by PortAudio on its audio thread; public only because the C callback needs it.
    int render(const void *input, void *output, unsigned long frames, unsigned long statusFlags);

private:
    [[nodiscard]] Result<StreamInfo>
    openStream(const DeviceConfig &config, const void *inputParameters, const void *outputParameters);

    bool m_initialized = false;
    void *m_stream = nullptr; // PaStream*
    IAudioProcessor *m_processor = nullptr;
    int m_inputChannels = 0;
    int m_outputChannels = 0;
    std::array<std::span<float>, rt::stereoChannels> m_inputSpans{};
    std::array<std::span<float>, rt::stereoChannels> m_outputSpans{};
};

} // namespace cc::audio
