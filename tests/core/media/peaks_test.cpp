// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/audiofile.h"
#include "core/media/peaks.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace cc;
using namespace cc::media;

namespace
{

/// Two seconds of stereo: silence, then a tone at half scale, then silence again.
std::filesystem::path writeFixture()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "peaks";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "tone.wav";
    constexpr int rate = 8000;
    std::vector<float> frames(static_cast<std::size_t>(rate * 2 * 2), 0.0F);
    for (int f = rate / 2; f < rate; ++f) { // 0.5 s .. 1.0 s
        const auto value = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * f / rate));
        frames[static_cast<std::size_t>(f) * 2] = value;
        frames[(static_cast<std::size_t>(f) * 2) + 1] = value;
    }
    AudioFileWriter writer;
    REQUIRE(writer.open(path, rate, 2, AudioFileFormat::WavFloat));
    REQUIRE(writer.write(frames));
    return path;
}

} // namespace

TEST_CASE("peaks: buckets hold the loudest sample of their span, silence stays at zero")
{
    const std::filesystem::path path = writeFixture();
    const std::stop_source stop;
    double last = 0.0;
    const Result<Peaks> peaks = computePeaks(path, 8, stop.get_token(), [&last](double f) {
        last = f;
    });
    REQUIRE(peaks);
    REQUIRE(peaks->buckets.size() == 8);
    REQUIRE(peaks->seconds == 2.0);
    REQUIRE(peaks->sampleRate == 8000.0);
    REQUIRE(peaks->buckets[0] == 0.0F); // 0.00..0.25 s
    REQUIRE(peaks->buckets[1] == 0.0F); // 0.25..0.50 s
    REQUIRE(peaks->buckets[2] > 0.45F); // 0.50..0.75 s: the tone
    REQUIRE(peaks->buckets[3] > 0.45F);
    REQUIRE(peaks->buckets[4] == 0.0F);
    REQUIRE(last == 1.0);
}

TEST_CASE("peaks: a missing file and a cancelled job say so")
{
    const std::stop_source stop;
    REQUIRE(computePeaks("nope.wav", 4, stop.get_token(), [](double) { }).error().code == ErrorCode::FileNotFound);
    std::stop_source cancelled;
    cancelled.request_stop();
    REQUIRE(computePeaks(writeFixture(), 4, cancelled.get_token(), [](double) { }).error().code
            == ErrorCode::Cancelled);
}
