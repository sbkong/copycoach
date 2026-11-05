// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The FFmpeg link (cmake/FFmpeg.cmake): the vcpkg build is the LGPL one, dynamically linked, and the decoders the
// video panel needs (H.264, HEVC, AAC) are compiled in (docs/features/video.md).
#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <string>

TEST_CASE("ffmpeg: LGPL build with the video and audio decoders the app needs")
{
    REQUIRE((avformat_version() >> 16U) == LIBAVFORMAT_VERSION_MAJOR);
    REQUIRE((avcodec_version() >> 16U) == LIBAVCODEC_VERSION_MAJOR);
    REQUIRE(swscale_version() > 0);
    REQUIRE(swresample_version() > 0);
    REQUIRE(std::string(avcodec_license()).starts_with("LGPL"));
    REQUIRE((avcodec_configuration() == nullptr || !std::string(avcodec_configuration()).contains("--enable-gpl")));
    REQUIRE(avcodec_find_decoder(AV_CODEC_ID_H264) != nullptr);
    REQUIRE(avcodec_find_decoder(AV_CODEC_ID_HEVC) != nullptr);
    REQUIRE(avcodec_find_decoder(AV_CODEC_ID_AAC) != nullptr);
    REQUIRE(av_find_input_format("mov,mp4,m4a,3gp,3g2,mj2") != nullptr);
}
