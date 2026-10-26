// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/nullbackend.h"

#include <algorithm>

namespace cc::audio
{

namespace
{

constexpr int defaultBufferFrames = 256;

void prepare(std::vector<std::vector<float>> &storage, std::vector<std::span<float>> &spans, int channels, int frames)
{
    storage.assign(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames), 0.0F));
    spans.clear();
    for (std::vector<float> &ch : storage) {
        spans.emplace_back(ch);
    }
}

} // namespace

Result<std::vector<DeviceInfo>> NullBackend::enumerate()
{
    return std::vector<DeviceInfo>{DeviceInfo{"null", "null", rt::stereoChannels, rt::stereoChannels, 48000.0}};
}

Result<StreamInfo> NullBackend::open(const DeviceConfig &config, IAudioProcessor &processor)
{
    if (m_open) {
        return fail(ErrorCode::DeviceBusy, "null");
    }
    if (config.outputChannels != rt::stereoChannels || config.sampleRate <= 0.0) {
        return fail(ErrorCode::DeviceOpenFailed, "null", "unsupported channel count or sample rate");
    }
    m_config = config;
    if (m_config.bufferFrames <= 0) {
        m_config.bufferFrames = defaultBufferFrames;
    }
    prepare(m_input, m_inputSpans, m_config.inputChannels, m_config.bufferFrames);
    prepare(m_output, m_outputSpans, m_config.outputChannels, m_config.bufferFrames);
    m_processor = &processor;
    m_open = true;
    return StreamInfo{m_config.sampleRate, 0.0, 0.0, m_config.bufferFrames};
}

Result<void> NullBackend::start()
{
    if (!m_open) {
        return fail(ErrorCode::DeviceOpenFailed, "null", "start before open");
    }
    m_running = true;
    return {};
}

Result<void> NullBackend::stop()
{
    if (!m_open) {
        return fail(ErrorCode::DeviceOpenFailed, "null", "stop before open");
    }
    m_running = false;
    return {};
}

void NullBackend::close()
{
    m_running = false;
    m_open = false;
    m_processor = nullptr;
}

bool NullBackend::isOpen() const
{
    return m_open;
}

bool NullBackend::isRunning() const
{
    return m_running;
}

bool NullBackend::pump(int frames, std::uint32_t statusFlags)
{
    if (!m_running) {
        return false;
    }
    const int count = std::clamp(frames, 0, m_config.bufferFrames);
    const auto n = static_cast<std::size_t>(count);
    for (std::size_t i = 0; i < m_input.size(); ++i) {
        m_inputSpans[i] = std::span<float>(m_input[i]).first(n);
        std::ranges::fill(m_inputSpans[i], 0.0F);
    }
    for (std::size_t i = 0; i < m_output.size(); ++i) {
        m_outputSpans[i] = std::span<float>(m_output[i]).first(n);
    }
    const rt::AudioBlock input{std::span<std::span<float>>(m_inputSpans), count};
    rt::AudioBlock output{std::span<std::span<float>>(m_outputSpans), count};
    m_processor->process(input, output, statusFlags);
    m_lastFrames = count;
    return true;
}

std::span<const float> NullBackend::output(std::size_t index) const
{
    return std::span<const float>(m_output[index]).first(static_cast<std::size_t>(m_lastFrames));
}

const DeviceConfig &NullBackend::config() const
{
    return m_config;
}

} // namespace cc::audio
