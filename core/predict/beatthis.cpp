// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/predict/beatthis.h"

#include "core/media/audiofile.h"
#include "core/media/resampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#if CC_HAVE_ORT
#include <onnxruntime_c_api.h>
#endif

namespace cc::predict
{

namespace
{

constexpr float maskValue = -1000.0F;
constexpr std::size_t peakHalfKernel = 3; // kernel 7
constexpr double dedupWidthFrames = 1.0;
constexpr double decodeShare = 0.2; // of the progress bar: decoding, then the model

std::vector<float> toMono(std::span<const float> interleaved, int channels)
{
    const std::size_t frames = interleaved.size() / static_cast<std::size_t>(channels);
    std::vector<float> mono(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        float sum = 0.0F;
        for (int c = 0; c < channels; ++c) {
            sum += interleaved[(f * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(c)];
        }
        mono[f] = sum / static_cast<float>(channels);
    }
    return mono;
}

Result<std::vector<float>> resampleTo22k(std::span<const float> mono, double rate)
{
    const double ratio = static_cast<double>(logMelSampleRate) / rate;
    media::Resampler resampler(1);
    std::vector<float> out(static_cast<std::size_t>(std::ceil(static_cast<double>(mono.size()) * ratio))
                           + logMelFftSize);
    std::size_t produced = 0;
    std::size_t consumed = 0;
    while (true) {
        const std::span<const float> input = mono.subspan(consumed);
        const std::span<float> output = std::span(out).subspan(produced);
        if (output.empty()) {
            out.resize(out.size() + logMelFftSize);
            continue;
        }
        Result<media::Resampler::Progress> step = resampler.process(input, output, ratio, true);
        if (!step) {
            return std::unexpected(step.error());
        }
        consumed += static_cast<std::size_t>(step->inputUsed);
        produced += static_cast<std::size_t>(step->outputProduced);
        if (step->outputProduced == 0) {
            break;
        }
    }
    out.resize(produced);
    return out;
}

} // namespace

Result<std::vector<float>>
loadMono22k(const std::filesystem::path &audio, const std::stop_token &stop, const jobs::Report &report)
{
    media::AudioFileReader reader;
    if (Result<void> r = reader.open(audio); !r) {
        return std::unexpected(r.error());
    }
    const media::AudioFileInfo &info = reader.info();
    std::vector<float> interleaved(static_cast<std::size_t>(info.frames) * static_cast<std::size_t>(info.channels));
    std::size_t filled = 0;
    constexpr std::size_t chunkFrames = 65536;
    while (filled < interleaved.size()) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, audio.string());
        }
        const std::size_t want =
            std::min(chunkFrames * static_cast<std::size_t>(info.channels), interleaved.size() - filled);
        const std::int64_t got = reader.read(std::span(interleaved).subspan(filled, want));
        if (got <= 0) {
            break;
        }
        filled += static_cast<std::size_t>(got) * static_cast<std::size_t>(info.channels);
        report(decodeShare * static_cast<double>(filled) / static_cast<double>(interleaved.size()));
    }
    interleaved.resize(filled);
    std::vector<float> mono = toMono(interleaved, info.channels);
    if (info.sampleRate == logMelSampleRate) {
        return mono;
    }
    return resampleTo22k(mono, info.sampleRate);
}

#if CC_HAVE_ORT

namespace
{

Error ortError(const OrtApi &api, OrtStatus *status, const std::string &subject)
{
    const std::string detail = api.GetErrorMessage(status);
    api.ReleaseStatus(status);
    return Error{ErrorCode::ModelCorrupt, subject, detail};
}

/// The ONNX Runtime objects of one model, released in reverse order.
class OrtSessionHolder
{
public:
    OrtSessionHolder() = default;
    ~OrtSessionHolder();
    OrtSessionHolder(const OrtSessionHolder &) = delete;
    OrtSessionHolder &operator=(const OrtSessionHolder &) = delete;
    OrtSessionHolder(OrtSessionHolder &&) = delete;
    OrtSessionHolder &operator=(OrtSessionHolder &&) = delete;

    [[nodiscard]] Result<void> open(const std::filesystem::path &model);
    /// One window [1, frames, 128] through the session into the beat and downbeat logits of its frames.
    [[nodiscard]] Result<void>
    run(std::span<const float> window, std::span<float> beat, std::span<float> downbeat) const;

private:
    [[nodiscard]] Result<void>
    copyOutputs(std::span<OrtValue *> outputs, std::span<float> beat, std::span<float> downbeat) const;

    const OrtApi *m_api = nullptr;
    OrtEnv *m_env = nullptr;
    OrtSessionOptions *m_options = nullptr;
    OrtSession *m_session = nullptr;
    OrtMemoryInfo *m_memory = nullptr;
};

OrtSessionHolder::~OrtSessionHolder()
{
    if (m_api == nullptr) {
        return;
    }
    if (m_memory != nullptr) {
        m_api->ReleaseMemoryInfo(m_memory);
    }
    if (m_session != nullptr) {
        m_api->ReleaseSession(m_session);
    }
    if (m_options != nullptr) {
        m_api->ReleaseSessionOptions(m_options);
    }
    if (m_env != nullptr) {
        m_api->ReleaseEnv(m_env);
    }
}

Result<void> OrtSessionHolder::open(const std::filesystem::path &model)
{
    m_api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (m_api == nullptr) {
        return fail(ErrorCode::Unsupported, "onnxruntime", "API version mismatch");
    }
    OrtStatus *status = m_api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "copycoach", &m_env);
    if (status == nullptr) {
        status = m_api->CreateSessionOptions(&m_options);
    }
    if (status == nullptr) {
        status = m_api->CreateSession(m_env, model.c_str(), m_options, &m_session);
    }
    if (status == nullptr) {
        status = m_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_memory);
    }
    if (status != nullptr) {
        return std::unexpected(ortError(*m_api, status, model.string()));
    }
    return {};
}

Result<void>
OrtSessionHolder::run(std::span<const float> window, std::span<float> beat, std::span<float> downbeat) const
{
    const std::array<std::int64_t, 3> shape{1,
                                            static_cast<std::int64_t>(beat.size()),
                                            static_cast<std::int64_t>(logMelBins)};
    OrtValue *input = nullptr;
    OrtStatus *status = m_api->CreateTensorWithDataAsOrtValue(
        m_memory,
        const_cast<float *>(window.data()), // NOLINT(cppcoreguidelines-pro-type-const-cast) ORT reads only
        window.size() * sizeof(float),
        shape.data(),
        shape.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input);
    if (status != nullptr) {
        return std::unexpected(ortError(*m_api, status, "beat_this"));
    }
    constexpr std::array<const char *, 1> inputNames{"spectrogram"};
    constexpr std::array<const char *, 2> outputNames{"beat", "downbeat"};
    std::array<OrtValue *, 2> outputs{nullptr, nullptr};
    status = m_api->Run(m_session,
                        nullptr,
                        inputNames.data(),
                        &input,
                        1,
                        outputNames.data(),
                        outputNames.size(),
                        outputs.data());
    m_api->ReleaseValue(input);
    if (status != nullptr) {
        return std::unexpected(ortError(*m_api, status, "beat_this"));
    }
    return copyOutputs(outputs, beat, downbeat);
}

Result<void>
OrtSessionHolder::copyOutputs(std::span<OrtValue *> outputs, std::span<float> beat, std::span<float> downbeat) const
{
    Result<void> result;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        float *data = nullptr;
        OrtStatus *status = m_api->GetTensorMutableData(
            outputs[i],
            reinterpret_cast<void **>(&data)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) C API
        if (status != nullptr) {
            result = std::unexpected(ortError(*m_api, status, "beat_this"));
        } else {
            const std::span<float> target = i == 0 ? beat : downbeat;
            std::copy_n(data, target.size(), target.begin());
        }
        m_api->ReleaseValue(outputs[i]);
    }
    return result;
}

/// Window starts of split_piece (beat_this.inference): from -border, stepping chunk - 2 border; the last one
/// is pulled back so it ends at the piece's end.
std::vector<std::ptrdiff_t> windowStarts(std::size_t total)
{
    const auto chunk = static_cast<std::ptrdiff_t>(beatThisChunkFrames);
    const auto border = static_cast<std::ptrdiff_t>(beatThisBorderFrames);
    const auto end = static_cast<std::ptrdiff_t>(total) + border;
    std::vector<std::ptrdiff_t> starts;
    for (std::ptrdiff_t start = -border;; start += chunk - (2 * border)) {
        if (start + chunk >= end) {
            starts.push_back(total > beatThisChunkFrames ? end - chunk : start);
            return starts;
        }
        starts.push_back(start);
    }
}

void fillWindow(const LogMel &frames, std::ptrdiff_t start, std::span<float> window)
{
    std::ranges::fill(window, 0.0F);
    const auto total = static_cast<std::ptrdiff_t>(frames.frames);
    for (std::size_t i = 0; i < beatThisChunkFrames; ++i) {
        const std::ptrdiff_t f = start + static_cast<std::ptrdiff_t>(i);
        if (f >= 0 && f < total) {
            std::copy_n(frames.values.begin() + (f * static_cast<std::ptrdiff_t>(logMelBins)),
                        logMelBins,
                        window.begin() + static_cast<std::ptrdiff_t>(i * logMelBins));
        }
    }
}

/// aggregate_prediction with keep_first: frames inside the border, not yet written, take this window's logits.
void keepFirst(std::ptrdiff_t start,
               std::span<const float> beat,
               std::span<const float> downbeat,
               BeatLogits &logits,
               std::vector<bool> &written)
{
    const auto total = static_cast<std::ptrdiff_t>(written.size());
    for (std::size_t i = beatThisBorderFrames; i + beatThisBorderFrames < beatThisChunkFrames; ++i) {
        const std::ptrdiff_t f = start + static_cast<std::ptrdiff_t>(i);
        if (f < 0 || f >= total || written[static_cast<std::size_t>(f)]) {
            continue;
        }
        logits.beat[static_cast<std::size_t>(f)] = beat[i];
        logits.downbeat[static_cast<std::size_t>(f)] = downbeat[i];
        written[static_cast<std::size_t>(f)] = true;
    }
}

} // namespace

Result<BeatLogits> runBeatThis(const LogMel &frames,
                               const std::filesystem::path &model,
                               const std::stop_token &stop,
                               const jobs::Report &report)
{
    OrtSessionHolder session;
    if (Result<void> r = session.open(model); !r) {
        return std::unexpected(r.error());
    }
    const std::size_t total = frames.frames;
    BeatLogits logits{std::vector<float>(total, maskValue), std::vector<float>(total, maskValue)};
    std::vector<bool> written(total, false);
    std::vector<float> window(beatThisChunkFrames * logMelBins);
    std::vector<float> beat(beatThisChunkFrames);
    std::vector<float> downbeat(beatThisChunkFrames);
    const std::vector<std::ptrdiff_t> starts = windowStarts(total);
    for (std::size_t w = 0; w < starts.size(); ++w) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, model.string());
        }
        fillWindow(frames, starts[w], window);
        if (Result<void> r = session.run(window, beat, downbeat); !r) {
            return std::unexpected(r.error());
        }
        keepFirst(starts[w], beat, downbeat, logits, written);
        report(decodeShare + ((1.0 - decodeShare) * static_cast<double>(w + 1) / static_cast<double>(starts.size())));
    }
    return logits;
}

#else

Result<BeatLogits>
runBeatThis(const LogMel &, const std::filesystem::path &model, const std::stop_token &, const jobs::Report &)
{
    return fail(ErrorCode::Unsupported, model.string(), "built without ONNX Runtime");
}

#endif

namespace
{

/// Frame indices of local maxima (kernel 7) above the threshold, duplicates within one frame merged.
std::vector<double> peaks(std::span<const float> logits)
{
    std::vector<double> frames;
    for (std::size_t i = 0; i < logits.size(); ++i) {
        if (logits[i] <= 0.0F) {
            continue;
        }
        bool top = true;
        const std::size_t from = i >= peakHalfKernel ? i - peakHalfKernel : 0;
        const std::size_t to = std::min(logits.size() - 1, i + peakHalfKernel);
        for (std::size_t k = from; k <= to && top; ++k) {
            top = logits[k] <= logits[i];
        }
        if (top) {
            frames.push_back(static_cast<double>(i));
        }
    }
    std::vector<double> merged;
    std::size_t count = 0;
    for (const double f : frames) {
        if (!merged.empty() && f - merged.back() <= dedupWidthFrames) {
            ++count;
            merged.back() += (f - merged.back()) / static_cast<double>(count);
        } else {
            merged.push_back(f);
            count = 1;
        }
    }
    return merged;
}

double nearestBeat(double seconds, std::span<const double> beats)
{
    if (beats.empty()) {
        return seconds;
    }
    return *std::ranges::min_element(beats, {}, [seconds](double b) {
        return std::abs(b - seconds);
    });
}

/// Each downbeat moved to the nearest beat, duplicates dropped (the paper's post-processing keeps the beat grid).
std::vector<double> snapToBeats(std::span<const double> downbeats, std::span<const double> beats)
{
    std::vector<double> out;
    for (const double raw : downbeats) {
        const double seconds = nearestBeat(raw, beats);
        if (out.empty() || out.back() != seconds) {
            out.push_back(seconds);
        }
    }
    return out;
}

} // namespace

BeatPrediction minimalPostprocess(const BeatLogits &logits, double fps)
{
    BeatPrediction out;
    for (const double f : peaks(logits.beat)) {
        out.beats.push_back(f / fps);
    }
    std::vector<double> downbeats;
    for (const double f : peaks(logits.downbeat)) {
        downbeats.push_back(f / fps);
    }
    out.downbeats = snapToBeats(downbeats, out.beats);
    out.seconds = static_cast<double>(logits.beat.size()) / fps;
    return out;
}

Result<BeatPrediction> predictBeats(const std::filesystem::path &audio,
                                    const BeatThisFiles &files,
                                    const std::stop_token &stop,
                                    const jobs::Report &report)
{
    LogMelFilterbank filterbank;
    if (Result<void> r = filterbank.load(files.filterbank); !r) {
        return std::unexpected(r.error());
    }
    Result<std::vector<float>> signal = loadMono22k(audio, stop, report);
    if (!signal) {
        return std::unexpected(signal.error());
    }
    const LogMel frames = logMel(*signal, filterbank);
    Result<BeatLogits> logits = runBeatThis(frames, files.model, stop, report);
    if (!logits) {
        return std::unexpected(logits.error());
    }
    BeatPrediction prediction = minimalPostprocess(*logits);
    prediction.seconds = static_cast<double>(signal->size()) / logMelSampleRate;
    return prediction;
}

model::Prediction toPrediction(const BeatPrediction &prediction, const std::string &sourceId)
{
    model::Prediction out;
    out.kind = model::PredictionKind::Beats;
    out.sourceId = sourceId;
    std::size_t bar = 0;
    for (const double seconds : prediction.beats) {
        model::PredictedItem item;
        item.seconds = seconds;
        const bool downbeat = std::ranges::find(prediction.downbeats, seconds) != prediction.downbeats.end();
        item.text = downbeat ? std::to_string(++bar) : std::string("beat");
        item.confidence = 1.0;
        out.items.push_back(std::move(item));
    }
    out.measureTo = bar > 0 ? bar - 1 : 0;
    return out;
}

} // namespace cc::predict
