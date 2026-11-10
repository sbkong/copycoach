// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The separation job (copy-spec.md section 16.2 D-2): a mono 48 kHz recording comes back as four stereo
// 44.1 kHz FLAC files of the resampled length in the stems folder. Skips without the exported model.
#include "core/media/audiofile.h"
#include "core/media/loadaudio.h"
#include "core/separation/separator.h"
#include "core/separation/spleetermodel.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace cc;
using namespace cc::separation;

namespace
{

const std::filesystem::path s_model = std::filesystem::path(CC_SPLEETER_DIR) / spleeterModel.name;

} // namespace

TEST_CASE("loadaudio: a mono 48 kHz file comes back as stereo 44.1 kHz frames")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "loadaudio";
    std::filesystem::create_directories(dir);
    const std::filesystem::path wav = dir / "mono48.wav";
    constexpr int rate = 48000;
    std::vector<float> mono(rate / 2);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        mono[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / rate));
    }
    media::AudioFileWriter writer;
    REQUIRE(writer.open(wav, rate, 1, media::AudioFileFormat::WavFloat));
    REQUIRE(writer.write(mono));
    REQUIRE(writer.close());
    const std::stop_token never;
    const Result<std::vector<float>> frames =
        media::loadResampled(wav, stemSampleRate, stemChannels, never, [](double) { });
    REQUIRE(frames);
    const std::size_t n = frames->size() / stemChannels;
    CHECK(std::abs(static_cast<double>(n) - (static_cast<double>(mono.size()) * 44100.0 / rate)) < 4.0);
    constexpr std::size_t probe = std::size_t{1000} * 2;
    CHECK((*frames)[probe] == (*frames)[probe + 1]); // both sides carry the mono signal
}

TEST_CASE("separator: the stems land as four FLAC files")
{
    if (!std::filesystem::exists(s_model)) {
        SKIP("spleeter-4stems.onnx not exported (tools/models/export_spleeter.py)");
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "separator";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path wav = dir / "mix.wav";
    constexpr int rate = 48000;
    std::vector<float> mono(rate);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        mono[i] = static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * 55.0 * static_cast<double>(i) / rate));
    }
    media::AudioFileWriter writer;
    REQUIRE(writer.open(wav, rate, 1, media::AudioFileFormat::WavFloat));
    REQUIRE(writer.write(mono));
    REQUIRE(writer.close());
    const std::stop_token never;
    const StemModel model{StemEngine::Spleeter, s_model};
    const auto paths = separateToFiles(wav, model, dir / "stems", never, [](double) { });
    REQUIRE(paths);
    for (std::size_t k = 0; k < stemCount; ++k) {
        CHECK(paths.value()[k].filename() == std::string(stemNames[k]) + ".flac");
        media::AudioFileReader reader;
        REQUIRE(reader.open(paths.value()[k]));
        CHECK(reader.info().sampleRate == stemSampleRate);
        CHECK(reader.info().channels == stemChannels);
        CHECK(std::abs(static_cast<double>(reader.info().frames) - 44100.0) < 4.0);
    }
}
