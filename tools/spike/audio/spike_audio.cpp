// Spike (c): PortAudio host-API enumeration + duplex stream + two-stem loop playback.
//
// Usage:
//   spike_audio list
//   spike_audio run <hostApi: asio|wasapi-ex|wasapi-sh|wdmks> [outDevIdx] [inDevIdx] [frames=256] [seconds=5]
//
// Prints the device model we care about (host API -> device -> channels), then opens a stream
// and reports: achieved latency, callback timing (max/avg), xrun flags, CPU load.
// Callback mixes two synthetic "stems" looped over a 2 s region and monitors the input if present.

#include <portaudio.h>
#include <pa_win_wasapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kChannels = 2;
constexpr int kLoopSeconds = 2;

struct Stem {
    std::vector<float> samples; // interleaved stereo
    float gain = 0.5f;
};

struct State {
    Stem stemA;
    Stem stemB;
    size_t loopFrames = 0;
    size_t position = 0;
    bool hasInput = false;

    // Written by the callback, read by the main thread (relaxed is fine for diagnostics).
    std::atomic<unsigned long> callbacks{0};
    std::atomic<unsigned long> xruns{0};
    std::atomic<double> maxCallbackUs{0.0};
    std::atomic<double> sumCallbackUs{0.0};
};

Stem makeTone(double hz, float gain)
{
    Stem stem;
    const size_t frames = static_cast<size_t>(kSampleRate) * kLoopSeconds;
    stem.samples.resize(frames * kChannels);
    for (size_t i = 0; i < frames; ++i) {
        // Short envelope every 0.5 s so the loop point is audible.
        const double t = static_cast<double>(i) / kSampleRate;
        const double env = std::exp(-6.0 * std::fmod(t, 0.5));
        const auto v = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * t) * env);
        stem.samples[i * kChannels] = v;
        stem.samples[i * kChannels + 1] = v;
    }
    stem.gain = gain;
    return stem;
}

int callback(const void *input, void *output, unsigned long frameCount, const PaStreamCallbackTimeInfo *,
             PaStreamCallbackFlags statusFlags, void *userData)
{
    const auto start = std::chrono::steady_clock::now();
    auto *state = static_cast<State *>(userData);
    auto *out = static_cast<float *>(output);
    const auto *in = static_cast<const float *>(input);

    if (statusFlags & (paInputUnderflow | paInputOverflow | paOutputUnderflow | paOutputOverflow)) {
        state->xruns.fetch_add(1, std::memory_order_relaxed);
    }

    for (unsigned long f = 0; f < frameCount; ++f) {
        const size_t idx = state->position * kChannels;
        for (int c = 0; c < kChannels; ++c) {
            float v = state->stemA.samples[idx + c] * state->stemA.gain + state->stemB.samples[idx + c] * state->stemB.gain;
            if (state->hasInput && in != nullptr) {
                v += in[f * kChannels + c] * 0.5f; // input monitor
            }
            out[f * kChannels + c] = v;
        }
        state->position = (state->position + 1) % state->loopFrames;
    }

    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    state->callbacks.fetch_add(1, std::memory_order_relaxed);
    state->sumCallbackUs.fetch_add(us, std::memory_order_relaxed);
    double prev = state->maxCallbackUs.load(std::memory_order_relaxed);
    while (us > prev && !state->maxCallbackUs.compare_exchange_weak(prev, us, std::memory_order_relaxed)) {
    }
    return paContinue;
}

void listDevices()
{
    const int apiCount = Pa_GetHostApiCount();
    for (int a = 0; a < apiCount; ++a) {
        const PaHostApiInfo *api = Pa_GetHostApiInfo(a);
        std::printf("[hostApi %d] %s  devices=%d  defaultIn=%d defaultOut=%d\n", a, api->name, api->deviceCount,
                    api->defaultInputDevice, api->defaultOutputDevice);
        for (int d = 0; d < api->deviceCount; ++d) {
            const PaDeviceIndex idx = Pa_HostApiDeviceIndexToDeviceIndex(a, d);
            const PaDeviceInfo *dev = Pa_GetDeviceInfo(idx);
            std::printf("    dev %2d  in=%2d out=%2d  lowLat in=%.1fms out=%.1fms  sr=%.0f  %s\n", idx, dev->maxInputChannels,
                        dev->maxOutputChannels, dev->defaultLowInputLatency * 1000.0, dev->defaultLowOutputLatency * 1000.0,
                        dev->defaultSampleRate, dev->name);
        }
    }
}

int findHostApi(PaHostApiTypeId type)
{
    const PaHostApiIndex idx = Pa_HostApiTypeIdToHostApiIndex(type);
    return idx < 0 ? -1 : idx;
}

int run(const std::string &mode, int outDev, int inDev, int frames, int seconds)
{
    const bool wasapi = mode.rfind("wasapi", 0) == 0;
    const PaHostApiTypeId apiType = wasapi ? paWASAPI : (mode == "wdmks" ? paWDMKS : paASIO);
    const int api = findHostApi(apiType);
    if (api < 0) {
        std::printf("host API not available: %s\n", mode.c_str());
        return 2;
    }
    const PaHostApiInfo *apiInfo = Pa_GetHostApiInfo(api);
    if (outDev < 0) {
        outDev = apiInfo->defaultOutputDevice;
    }
    if (inDev < 0) {
        inDev = apiInfo->defaultInputDevice;
    }
    if (outDev < 0) {
        std::printf("no output device for %s\n", apiInfo->name);
        return 2;
    }

    PaWasapiStreamInfo wasapiInfo{};
    wasapiInfo.size = sizeof(PaWasapiStreamInfo);
    wasapiInfo.hostApiType = paWASAPI;
    wasapiInfo.version = 1;
    wasapiInfo.flags = (mode == "wasapi-ex") ? paWinWasapiExclusive : 0;
    wasapiInfo.threadPriority = eThreadPriorityProAudio;
    if (mode == "wasapi-ex") {
        wasapiInfo.flags |= paWinWasapiThreadPriority;
    }

    PaStreamParameters outParams{};
    outParams.device = outDev;
    outParams.channelCount = kChannels;
    outParams.sampleFormat = paFloat32;
    outParams.suggestedLatency = Pa_GetDeviceInfo(outDev)->defaultLowOutputLatency;
    outParams.hostApiSpecificStreamInfo = wasapi ? &wasapiInfo : nullptr;

    PaStreamParameters inParams{};
    const bool hasInput = inDev >= 0 && Pa_GetDeviceInfo(inDev)->maxInputChannels >= kChannels;
    if (hasInput) {
        inParams.device = inDev;
        inParams.channelCount = kChannels;
        inParams.sampleFormat = paFloat32;
        inParams.suggestedLatency = Pa_GetDeviceInfo(inDev)->defaultLowInputLatency;
        inParams.hostApiSpecificStreamInfo = wasapi ? &wasapiInfo : nullptr;
    }

    State state;
    state.stemA = makeTone(220.0, 0.4f);
    state.stemB = makeTone(330.0, 0.3f);
    state.loopFrames = state.stemA.samples.size() / kChannels;
    state.hasInput = hasInput;

    PaStream *stream = nullptr;
    PaError err = Pa_OpenStream(&stream, hasInput ? &inParams : nullptr, &outParams, kSampleRate, frames, paClipOff,
                                callback, &state);
    if (err != paNoError) {
        std::printf("Pa_OpenStream failed: %s (%d)\n", Pa_GetErrorText(err), err);
        if (err == paUnanticipatedHostError) {
            const PaHostErrorInfo *h = Pa_GetLastHostErrorInfo();
            std::printf("  host error %ld: %s\n", h->errorCode, h->errorText);
        }
        return 3;
    }
    const PaStreamInfo *info = Pa_GetStreamInfo(stream);
    std::printf("opened %s  out=%d(%s)  in=%s  frames=%d  sr=%.0f  latency in=%.2fms out=%.2fms\n", apiInfo->name, outDev,
                Pa_GetDeviceInfo(outDev)->name, hasInput ? Pa_GetDeviceInfo(inDev)->name : "(none)", frames, info->sampleRate,
                info->inputLatency * 1000.0, info->outputLatency * 1000.0);

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::printf("Pa_StartStream failed: %s\n", Pa_GetErrorText(err));
        return 3;
    }
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    const double cpu = Pa_GetStreamCpuLoad(stream);
    Pa_StopStream(stream);
    Pa_CloseStream(stream);

    const unsigned long n = state.callbacks.load();
    std::printf("callbacks=%lu  xruns=%lu  cpuLoad=%.1f%%  callback avg=%.1fus max=%.1fus  budget=%.1fus\n", n,
                state.xruns.load(), cpu * 100.0, n ? state.sumCallbackUs.load() / n : 0.0, state.maxCallbackUs.load(),
                frames / kSampleRate * 1e6);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("usage: spike_audio list | run <asio|wasapi-ex|wasapi-sh|wdmks> [outDev] [inDev] [frames] [seconds]\n");
        return 1;
    }
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::printf("Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }
    std::printf("%s\n", Pa_GetVersionText());

    int rc = 0;
    const std::string cmd = argv[1];
    if (cmd == "list") {
        listDevices();
    } else if (cmd == "run" && argc >= 3) {
        const int outDev = argc > 3 ? std::atoi(argv[3]) : -1;
        const int inDev = argc > 4 ? std::atoi(argv[4]) : -1;
        const int frames = argc > 5 ? std::atoi(argv[5]) : 256;
        const int seconds = argc > 6 ? std::atoi(argv[6]) : 5;
        rc = run(argv[2], outDev, inDev, frames, seconds);
    } else {
        rc = 1;
    }
    Pa_Terminate();
    return rc;
}
