// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/audiofile.h"
#include "core/predict/beatthis.h"
#include "core/predict/beatthismodel.h"
#include "core/predict/logmel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <vector>

using namespace cc;
using namespace cc::predict;

namespace
{

const std::filesystem::path s_modelDir = CC_BEAT_THIS_DIR; // the export output; tests needing it skip when absent
const std::filesystem::path s_fixtureDir = std::filesystem::path(CC_FIXTURE_DIR) / "predict";

std::vector<float> readFloats(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    file.seekg(0, std::ios::end);
    const auto bytes = static_cast<std::size_t>(file.tellg());
    file.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    file.read(reinterpret_cast<char *>(out.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
              static_cast<std::streamsize>(bytes));
    return out;
}

bool filterbankLoaded(LogMelFilterbank &fb)
{
    const std::filesystem::path path = s_modelDir / beatThisFilterbank.name;
    if (!std::filesystem::exists(path)) {
        return false;
    }
    REQUIRE(fb.load(path));
    return true;
}

/// Eight seconds of 44.1 kHz stereo clicks at 120 BPM, the first of every four louder (bars of 4/4).
std::filesystem::path writeClickTrack()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "predict";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "clicks.wav";
    constexpr int rate = 44100;
    constexpr int seconds = 8;
    std::vector<float> frames(static_cast<std::size_t>(rate) * seconds * 2, 0.0F);
    for (int beat = 0; beat < seconds * 2; ++beat) {
        const int start = beat * rate / 2;
        const float gain = beat % 4 == 0 ? 0.9F : 0.5F;
        const double freq = beat % 4 == 0 ? 1200.0 : 800.0;
        for (int i = 0; i < rate / 20; ++i) { // 50 ms decaying tone
            const double env = std::exp(-8.0 * i / (rate / 20.0));
            const auto v = static_cast<float>(gain * env * std::sin(2.0 * std::numbers::pi * freq * i / rate));
            const std::size_t f = static_cast<std::size_t>(start) + static_cast<std::size_t>(i);
            frames[f * 2] = v;
            frames[(f * 2) + 1] = v;
        }
    }
    media::AudioFileWriter writer;
    REQUIRE(writer.open(path, rate, 2, media::AudioFileFormat::WavFloat));
    REQUIRE(writer.write(frames));
    return path;
}

} // namespace

TEST_CASE("logmel: matches beat_this.preprocessing.LogMelSpect on the reference signal")
{
    LogMelFilterbank fb;
    if (!filterbankLoaded(fb)) {
        SKIP("mel-filterbank.bin not exported (tools/models/export_beat_this.py)");
    }
    const std::vector<float> signal = readFloats(s_fixtureDir / "logmel-signal.f32");
    const std::vector<float> reference = readFloats(s_fixtureDir / "logmel-reference.f32");
    REQUIRE(signal.size() == 44100);
    const LogMel mel = logMel(signal, fb);
    REQUIRE(mel.frames == 101);
    REQUIRE(mel.values.size() == reference.size());
    float worst = 0.0F;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        worst = std::max(worst, std::abs(mel.values[i] - reference[i]));
    }
    CHECK(worst < 1e-3F);
}

TEST_CASE("logmel: the filterbank loader rejects a file of the wrong size")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "predict";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "short.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file << "nope";
    }
    LogMelFilterbank fb;
    const Result<void> r = fb.load(path);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::FileCorrupt);
    CHECK_FALSE(fb.loaded());
}

TEST_CASE("beatthis: minimal post-processing picks peaks above zero and snaps downbeats to beats")
{
    BeatLogits logits;
    logits.beat.assign(400, -5.0F);
    logits.downbeat.assign(400, -5.0F);
    for (std::size_t f = 0; f < 400; f += 25) { // a beat every half second at 50 fps
        logits.beat[f] = 3.0F;
        logits.beat[f + 1] = 2.0F; // a neighbour below the peak is not a second beat
    }
    logits.downbeat[1] = 2.0F; // off by a frame: snaps to the beat at 0
    logits.downbeat[100] = 2.5F;
    logits.downbeat[200] = -1.0F; // below the threshold
    logits.downbeat[300] = 1.0F;
    const BeatPrediction p = minimalPostprocess(logits);
    REQUIRE(p.beats.size() == 16);
    CHECK(p.beats[1] == 0.5);
    REQUIRE(p.downbeats.size() == 3);
    CHECK(p.downbeats[0] == 0.0);
    CHECK(p.downbeats[1] == 2.0);
    CHECK(p.downbeats[2] == 6.0);
    CHECK(p.seconds == 8.0);

    const model::Prediction layer = toPrediction(p, "src-1");
    CHECK(layer.kind == model::PredictionKind::Beats);
    CHECK(layer.items.size() == 16);
    CHECK(layer.items[0].text == "1");
    CHECK(layer.items[1].text == "beat");
    CHECK(layer.items[4].text == "2");
    CHECK(layer.measureTo == 2);
}

TEST_CASE("beatthis: two logits one frame apart merge into their mean")
{
    BeatLogits logits;
    logits.beat.assign(50, -5.0F);
    logits.downbeat.assign(50, -5.0F);
    logits.beat[10] = 2.0F;
    logits.beat[11] = 2.0F;
    const BeatPrediction p = minimalPostprocess(logits);
    REQUIRE(p.beats.size() == 1);
    CHECK(p.beats[0] == 10.5 / logMelFps);
}

TEST_CASE("beatthis: the small model finds the beats of a click track")
{
    const BeatThisFiles files{s_modelDir / beatThisSmall.name, s_modelDir / beatThisFilterbank.name};
    if (!std::filesystem::exists(files.model) || !std::filesystem::exists(files.filterbank)) {
        SKIP("beat_this-small0.onnx not exported (tools/models/export_beat_this.py)");
    }
    const std::filesystem::path audio = writeClickTrack();
    std::vector<double> progress;
    const std::stop_token stop;
    const Result<BeatPrediction> result = predictBeats(audio, files, stop, [&progress](double p) {
        progress.push_back(p);
    });
    REQUIRE(result);
    CHECK(result->seconds == 8.0);
    REQUIRE(result->beats.size() >= 12);
    CHECK(result->beats.size() <= 18);
    for (std::size_t i = 1; i < result->beats.size(); ++i) {
        const double gap = result->beats[i] - result->beats[i - 1];
        CHECK(gap > 0.3);
        CHECK(gap < 0.7);
    }
    CHECK(!result->downbeats.empty());
    CHECK(std::ranges::is_sorted(progress));
    CHECK(progress.back() == 1.0);
}

TEST_CASE("beatthis: a stop request between windows reports Cancelled")
{
    const std::filesystem::path model = s_modelDir / beatThisSmall.name;
    if (!std::filesystem::exists(model)) {
        SKIP("beat_this-small0.onnx not exported");
    }
    std::stop_source source;
    source.request_stop();
    LogMel frames;
    frames.frames = 100;
    frames.values.assign(100 * logMelBins, 0.0F);
    const Result<BeatLogits> r = runBeatThis(frames, model, source.get_token(), [](double) { });
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Cancelled);
}
