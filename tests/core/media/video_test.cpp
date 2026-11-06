// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// File video through FFmpeg (video.md section 5): probe, audio extraction to FLAC, frames by time. The fixture
// is twenty grey frames at 10 fps (frame k = grey 12k + 6, mpeg4) with a two-second stereo tone (aac).
#include "core/media/audiofile.h"
#include "core/media/videofile.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace cc;
using namespace cc::media;

namespace
{

const std::filesystem::path s_fixture = std::filesystem::path(CC_FIXTURE_DIR) / "video" / "gray-steps.mp4";

/// Mean of the red channel: the frame's grey level, give or take the codec.
double greyOf(const VideoFrame &frame)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < frame.rgb.size(); i += 3) {
        sum += frame.rgb[i];
    }
    return sum / (static_cast<double>(frame.rgb.size()) / 3.0);
}

constexpr double greyTolerance = 10.0;

} // namespace

TEST_CASE("video: probe reports size, rate, length and the audio streams")
{
    const Result<VideoInfo> info = probeVideo(s_fixture);
    REQUIRE(info);
    CHECK(info->width == 64);
    CHECK(info->height == 48);
    CHECK(info->fps == 10.0);
    CHECK(std::abs(info->seconds - 2.0) < 0.1);
    CHECK(info->audioStreams == 1);

    const Result<VideoInfo> missing = probeVideo(s_fixture.parent_path() / "nope.mp4");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == ErrorCode::FileNotFound);
}

TEST_CASE("video: the audio track lands as a stereo FLAC of the same length")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "video";
    std::filesystem::create_directories(dir);
    const std::filesystem::path flac = dir / "extracted.flac";
    std::vector<double> progress;
    const std::stop_token never;
    REQUIRE(extractAudio(s_fixture, 0, flac, never, [&progress](double p) {
        progress.push_back(p);
    }));
    CHECK(progress.back() == 1.0);
    AudioFileReader reader;
    REQUIRE(reader.open(flac));
    CHECK(reader.info().channels == 2);
    CHECK(reader.info().sampleRate == 44100.0);
    CHECK(std::abs((static_cast<double>(reader.info().frames) / 44100.0) - 2.0) < 0.15);
    std::vector<float> samples(static_cast<std::size_t>(44100) * 2);
    REQUIRE(reader.read(samples) == 44100);
    float peak = 0.0F;
    for (const float s : samples) {
        peak = std::max(peak, std::abs(s));
    }
    CHECK(peak > 0.05F); // the tone is there (lavfi sine is quiet)

    const Result<void> noTrack = extractAudio(s_fixture, 3, dir / "none.flac", never, [](double) { });
    REQUIRE_FALSE(noTrack);
    CHECK(noTrack.error().code == ErrorCode::Unsupported);

    std::stop_source cancel;
    cancel.request_stop();
    const Result<void> cancelled = extractAudio(s_fixture, 0, dir / "cancel.flac", cancel.get_token(), [](double) { });
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
    CHECK_FALSE(std::filesystem::exists(dir / "cancel.flac"));
}

TEST_CASE("video: frames by time, forward decoding and seeking back both land on the right frame")
{
    VideoReader reader;
    REQUIRE(reader.open(s_fixture));
    CHECK(reader.info().fps == 10.0);

    Result<VideoFrame> frame = reader.frameAt(0.55); // frame 5
    REQUIRE(frame);
    CHECK(frame->width == 64);
    CHECK(frame->height == 48);
    CHECK(std::abs(frame->seconds - 0.5) < 0.01);
    CHECK(std::abs(greyOf(*frame) - 66.0) < greyTolerance);

    frame = reader.frameAt(1.25); // forward: frame 12
    REQUIRE(frame);
    CHECK(std::abs(frame->seconds - 1.2) < 0.01);
    CHECK(std::abs(greyOf(*frame) - 150.0) < greyTolerance);

    frame = reader.frameAt(1.27); // same frame again
    REQUIRE(frame);
    CHECK(std::abs(frame->seconds - 1.2) < 0.01);

    frame = reader.frameAt(0.05); // back: frame 0
    REQUIRE(frame);
    CHECK(std::abs(frame->seconds - 0.0) < 0.01);
    CHECK(std::abs(greyOf(*frame) - 6.0) < greyTolerance);

    frame = reader.frameAt(5.0); // past the end: the last frame
    REQUIRE(frame);
    CHECK(std::abs(frame->seconds - 1.9) < 0.01);
    CHECK(std::abs(greyOf(*frame) - 234.0) < greyTolerance);

    reader.close();
    CHECK_FALSE(reader.isOpen());
}
