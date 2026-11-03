// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Streaming audio files and resampling (audio-engine.md section 15.1 / 15.7).
#include "core/media/audiofile.h"
#include "core/media/resampler.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <span>
#include <vector>

using namespace cc;
using namespace cc::media;

namespace
{

std::filesystem::path tempDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "media";
    std::filesystem::create_directories(dir);
    return dir;
}

/// Interleaved stereo: left = sine at `hz`, right = the same at half amplitude.
std::vector<float> stereoSine(double sampleRate, double hz, std::int64_t frames)
{
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    for (std::int64_t f = 0; f < frames; ++f) {
        const auto value =
            static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(f) / sampleRate));
        out[static_cast<std::size_t>(f) * 2] = value;
        out[(static_cast<std::size_t>(f) * 2) + 1] = value * 0.5F;
    }
    return out;
}

int zeroCrossings(std::span<const float> mono)
{
    int count = 0;
    for (std::size_t i = 1; i < mono.size(); ++i) {
        if ((mono[i - 1] < 0.0F) != (mono[i] < 0.0F)) {
            ++count;
        }
    }
    return count;
}

void writeFile(const std::filesystem::path &path, double rate, const std::vector<float> &stereo, AudioFileFormat format)
{
    AudioFileWriter writer;
    REQUIRE(writer.open(path, rate, 2, format));
    REQUIRE(writer.write(stereo));
    REQUIRE(writer.close());
}

} // namespace

TEST_CASE("audio file: FLAC round-trips a stereo signal within 16-bit precision and seeks by frame")
{
    const auto path = tempDir() / "roundtrip.flac";
    const std::vector<float> stereo = stereoSine(44100.0, 440.0, 44100);
    writeFile(path, 44100.0, stereo, AudioFileFormat::Flac16);

    AudioFileReader reader;
    REQUIRE(reader.open(path));
    REQUIRE(reader.info().sampleRate == 44100.0);
    REQUIRE(reader.info().channels == 2);
    REQUIRE(reader.info().frames == 44100);
    REQUIRE(reader.info().format == "FLAC");

    std::vector<float> chunk(std::size_t{1024} * 2);
    REQUIRE(reader.read(chunk) == 1024);
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        REQUIRE(std::abs(chunk[i] - stereo[i]) < 2e-4F);
    }
    REQUIRE(reader.seek(44100 - 100));
    REQUIRE(reader.read(chunk) == 100); // the tail is shorter than the chunk
    REQUIRE(std::abs(chunk[0] - stereo[std::size_t{44100 - 100} * 2]) < 2e-4F);
    REQUIRE(reader.read(chunk) == 0);
}

TEST_CASE("audio file: float WAV is exact and readPlanar de-interleaves")
{
    const auto path = tempDir() / "planar.wav";
    const std::vector<float> stereo = stereoSine(48000.0, 1000.0, 4800);
    writeFile(path, 48000.0, stereo, AudioFileFormat::WavFloat);

    AudioFileReader reader;
    REQUIRE(reader.open(path));
    REQUIRE(reader.info().format == "WAV");
    std::vector<float> left(4800);
    std::vector<float> right(4800);
    const std::array<std::span<float>, 2> planes{std::span<float>(left), std::span<float>(right)};
    REQUIRE(reader.readPlanar(planes) == 4800);
    for (std::size_t f = 0; f < 4800; ++f) {
        REQUIRE(left[f] == stereo[f * 2]);
        REQUIRE(right[f] == stereo[(f * 2) + 1]);
    }
    REQUIRE(reader.readPlanar(planes) == 0);
    REQUIRE(left[0] == 0.0F); // a short read zero-fills the rest
}

TEST_CASE("audio file: missing and junk files come back as errors with the path as subject")
{
    AudioFileReader reader;
    const auto missing = reader.open(tempDir() / "missing.flac");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().code == ErrorCode::FileNotFound);
    REQUIRE(missing.error().subject.ends_with("missing.flac"));

    const auto junkPath = tempDir() / "junk.flac";
    std::ofstream(junkPath, std::ios::binary) << "this is not audio";
    const auto junk = reader.open(junkPath);
    REQUIRE_FALSE(junk);
    REQUIRE(junk.error().code == ErrorCode::FileCorrupt);
    REQUIRE_FALSE(reader.isOpen());
}

TEST_CASE("resampler: 44.1 k to 48 k keeps the frequency and scales the length")
{
    const std::int64_t frames = 44100;
    const std::vector<float> stereo = stereoSine(44100.0, 1000.0, frames);
    Resampler resampler(2);
    REQUIRE(resampler.channels() == 2);
    std::vector<float> out(std::size_t{48000 + 256} * 2);
    const double ratio = 48000.0 / 44100.0;
    const auto progress = resampler.process(stereo, out, ratio, true);
    REQUIRE(progress);
    REQUIRE(progress->inputUsed == frames);
    REQUIRE(std::abs(progress->outputProduced - 48000) <= 64);

    std::vector<float> left(static_cast<std::size_t>(progress->outputProduced));
    for (std::size_t f = 0; f < left.size(); ++f) {
        left[f] = out[f * 2];
    }
    const std::span<const float> middle =
        std::span<const float>(left).subspan(1000, left.size() - 2000); // skip the filter edges
    const int crossings = zeroCrossings(middle);
    const double expected = 2.0 * 1000.0 * static_cast<double>(middle.size()) / 48000.0;
    REQUIRE(std::abs(crossings - expected) <= 4.0);

    resampler.reset();
    const auto again = resampler.process(stereo, out, ratio, true);
    REQUIRE(again);
    REQUIRE(again->outputProduced == progress->outputProduced);
}
