// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/portaudiobackend.h"

#include <portaudio.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace cc::audio
{

namespace
{

constexpr std::string_view backendName = "portaudio";

std::string subject(std::string_view text)
{
    return std::string(text);
}

std::string_view hostApiName(PaHostApiTypeId type)
{
    switch (type) {
    case paASIO:
        return "asio";
    case paWASAPI:
        return "wasapi";
    case paMME:
        return "mme";
    case paDirectSound:
        return "directsound";
    case paWDMKS:
        return "wdm-ks";
    default:
        return "other";
    }
}

std::string paText(PaError error)
{
    return std::string(Pa_GetErrorText(error)) + " (" + std::to_string(error) + ")";
}

/// Host API index for the config's backend name ("asio", "wasapi-shared", "wasapi-exclusive"); the default
/// host when that API is not present.
int hostApiFor(const std::string &backend)
{
    const PaHostApiTypeId wanted = backend == "asio" ? paASIO : paWASAPI;
    const PaHostApiIndex index = Pa_HostApiTypeIdToHostApiIndex(wanted);
    return index >= 0 ? index : Pa_GetDefaultHostApi();
}

/// Device index for `wanted` within the host API, or the host's default device when the name is empty or unknown.
Result<int> resolveDevice(const DeviceConfig &config, bool input)
{
    const int host = hostApiFor(config.hostApi);
    const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo(host);
    if (hostInfo == nullptr) {
        return fail(ErrorCode::DeviceOpenFailed, config.hostApi, "host API not available");
    }
    const std::string &wanted = input ? config.inputDevice : config.outputDevice;
    for (int i = 0; i < hostInfo->deviceCount; ++i) {
        const PaDeviceIndex device = Pa_HostApiDeviceIndexToDeviceIndex(host, i);
        if (!wanted.empty() && wanted == Pa_GetDeviceInfo(device)->name) {
            return device;
        }
    }
    const PaDeviceIndex fallback = input ? hostInfo->defaultInputDevice : hostInfo->defaultOutputDevice;
    if (fallback == paNoDevice) {
        return fail(ErrorCode::DeviceOpenFailed, wanted.empty() ? config.hostApi : wanted, "no such device");
    }
    return fallback;
}

PaStreamParameters streamParameters(int device, int channels, bool input)
{
    const PaDeviceInfo *info = Pa_GetDeviceInfo(device);
    return PaStreamParameters{device,
                              std::min(channels, rt::stereoChannels),
                              paFloat32 | paNonInterleaved,
                              input ? info->defaultLowInputLatency : info->defaultLowOutputLatency,
                              nullptr};
}

// PortAudio's callback signature has six parameters; this thunk only forwards to the backend.
// NOLINTNEXTLINE(readability-function-size)
int callbackThunk(const void *input,
                  void *output,
                  unsigned long frames,
                  const PaStreamCallbackTimeInfo * /*time*/,
                  PaStreamCallbackFlags statusFlags,
                  void *userData)
{
    return static_cast<PortAudioBackend *>(userData)->render(input, output, frames, statusFlags);
}

std::uint32_t toStatusFlags(unsigned long paFlags)
{
    std::uint32_t flags = 0;
    if ((paFlags & paInputOverflow) != 0UL) {
        flags |= statusInputOverflow;
    }
    if ((paFlags & paOutputUnderflow) != 0UL) {
        flags |= statusOutputUnderflow;
    }
    return flags;
}

} // namespace

PortAudioBackend::PortAudioBackend()
    : m_initialized(Pa_Initialize() == paNoError)
{
}

PortAudioBackend::~PortAudioBackend()
{
    close();
    if (m_initialized) {
        Pa_Terminate();
    }
}

Result<std::vector<DeviceInfo>> PortAudioBackend::enumerate()
{
    if (!m_initialized) {
        return fail(ErrorCode::DeviceOpenFailed, subject(backendName), "Pa_Initialize failed");
    }
    std::vector<DeviceInfo> devices;
    const PaDeviceIndex count = Pa_GetDeviceCount();
    for (PaDeviceIndex i = 0; i < count; ++i) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        const PaHostApiInfo *host = Pa_GetHostApiInfo(info->hostApi);
        devices.push_back(DeviceInfo{std::string(hostApiName(host->type)),
                                     info->name,
                                     info->maxInputChannels,
                                     info->maxOutputChannels,
                                     info->defaultSampleRate});
    }
    return devices;
}

Result<StreamInfo> PortAudioBackend::open(const DeviceConfig &config, IAudioProcessor &processor)
{
    if (!m_initialized) {
        return fail(ErrorCode::DeviceOpenFailed, subject(backendName), "Pa_Initialize failed");
    }
    if (m_stream != nullptr) {
        return fail(ErrorCode::DeviceBusy, subject(backendName), "stream already open");
    }
    const auto outputDevice = resolveDevice(config, false);
    if (!outputDevice) {
        return std::unexpected(outputDevice.error());
    }
    const PaStreamParameters out = streamParameters(*outputDevice, config.outputChannels, false);
    const auto inputDevice = config.inputChannels > 0 ? resolveDevice(config, true) : Result<int>(paNoDevice);
    const bool withInput = inputDevice && *inputDevice != paNoDevice;
    const PaStreamParameters in =
        withInput ? streamParameters(*inputDevice, config.inputChannels, true) : PaStreamParameters{};
    m_inputChannels = withInput ? in.channelCount : 0;
    m_outputChannels = out.channelCount;
    m_processor = &processor;
    const unsigned long frames =
        config.bufferFrames > 0 ? static_cast<unsigned long>(config.bufferFrames) : paFramesPerBufferUnspecified;
    PaStream *stream = nullptr;
    const PaError err = Pa_OpenStream(&stream,
                                      withInput ? &in : nullptr,
                                      &out,
                                      config.sampleRate,
                                      frames,
                                      paClipOff,
                                      callbackThunk,
                                      this);
    if (err != paNoError) {
        m_processor = nullptr;
        return fail(ErrorCode::DeviceOpenFailed, Pa_GetDeviceInfo(*outputDevice)->name, paText(err));
    }
    m_stream = stream;
    const PaStreamInfo *info = Pa_GetStreamInfo(stream);
    return StreamInfo{info->sampleRate, info->inputLatency, info->outputLatency, config.bufferFrames};
}

Result<void> PortAudioBackend::start()
{
    if (m_stream == nullptr) {
        return fail(ErrorCode::DeviceOpenFailed, subject(backendName), "start before open");
    }
    const PaError err = Pa_StartStream(static_cast<PaStream *>(m_stream));
    if (err != paNoError && err != paStreamIsNotStopped) {
        return fail(ErrorCode::DeviceOpenFailed, subject(backendName), paText(err));
    }
    return {};
}

Result<void> PortAudioBackend::stop()
{
    if (m_stream == nullptr) {
        return fail(ErrorCode::DeviceOpenFailed, subject(backendName), "stop before open");
    }
    const PaError err = Pa_StopStream(static_cast<PaStream *>(m_stream));
    if (err != paNoError && err != paStreamIsStopped) {
        return fail(ErrorCode::DeviceOpenFailed, subject(backendName), paText(err));
    }
    return {};
}

void PortAudioBackend::close()
{
    if (m_stream != nullptr) {
        Pa_CloseStream(static_cast<PaStream *>(m_stream));
        m_stream = nullptr;
    }
    m_processor = nullptr;
}

bool PortAudioBackend::isOpen() const
{
    return m_stream != nullptr;
}

bool PortAudioBackend::isRunning() const
{
    return m_stream != nullptr && Pa_IsStreamActive(static_cast<PaStream *>(m_stream)) == 1;
}

int PortAudioBackend::render(const void *input, void *output, unsigned long frames, unsigned long statusFlags)
{
    const auto count = static_cast<int>(frames);
    const auto inputs = static_cast<std::size_t>(m_inputChannels);
    const auto outputs = static_cast<std::size_t>(m_outputChannels);
    // PortAudio hands the input as const void*; AudioBlock views are mutable spans, so the constness is dropped
    // once here (the input buffers are never written).
    const std::span<float *const> in(
        static_cast<float *const *>(const_cast<void *>(input)), // NOLINT(cppcoreguidelines-pro-type-const-cast)
        inputs);
    const std::span<float *const> out(static_cast<float *const *>(output), outputs);
    for (std::size_t ch = 0; ch < inputs; ++ch) {
        m_inputSpans[ch] = std::span<float>(in[ch], frames);
    }
    for (std::size_t ch = 0; ch < outputs; ++ch) {
        m_outputSpans[ch] = std::span<float>(out[ch], frames);
    }
    const rt::AudioBlock inputBlock{std::span<std::span<float>>(m_inputSpans).first(inputs), count};
    rt::AudioBlock outputBlock{std::span<std::span<float>>(m_outputSpans).first(outputs), count};
    m_processor->process(inputBlock, outputBlock, toStatusFlags(statusFlags));
    return paContinue;
}

} // namespace cc::audio
