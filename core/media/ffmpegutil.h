// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

// Owning handles and the steps every FFmpeg path shares (videofile.cpp, videoreader.cpp): open, pick a
// decoder, and the send/receive pump of the decoding API.
namespace cc::media
{

struct FormatDeleter {
    void operator()(AVFormatContext *ctx) const
    {
        avformat_close_input(&ctx);
    }
};
struct CodecDeleter {
    void operator()(AVCodecContext *ctx) const
    {
        avcodec_free_context(&ctx);
    }
};
struct PacketDeleter {
    void operator()(AVPacket *packet) const
    {
        av_packet_free(&packet);
    }
};
struct FrameDeleter {
    void operator()(AVFrame *frame) const
    {
        av_frame_free(&frame);
    }
};
struct SwrDeleter {
    void operator()(SwrContext *ctx) const
    {
        swr_free(&ctx);
    }
};
struct SwsDeleter {
    void operator()(SwsContext *ctx) const
    {
        sws_freeContext(ctx);
    }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;

[[nodiscard]] std::string ffmpegError(int code);
[[nodiscard]] std::string urlOf(const AVFormatContext &format);
/// FileNotFound when the path is missing, FileCorrupt when FFmpeg cannot read it; streams are probed.
[[nodiscard]] Result<void> openFormat(const std::filesystem::path &path, FormatPtr &format);
/// A decoder for the stream; Unsupported when FFmpeg has none for its codec.
[[nodiscard]] Result<void> openDecoder(const AVFormatContext &format, int streamIndex, CodecPtr &codec);

enum class Receive : std::uint8_t {
    Frame, // `frame` holds a decoded frame; unref it when done
    NeedPacket, // feed the decoder before asking again
    End, // the flush went through: nothing more
};
[[nodiscard]] Result<Receive> receiveFrame(AVCodecContext &codec, AVFrame &frame, const std::string &subject);
/// Reads packets until one of the stream and sends it; at the end of the file sends the flush instead.
[[nodiscard]] Result<void>
feedPacket(AVFormatContext &format, int streamIndex, AVPacket &packet, AVCodecContext &codec);
/// The next decoded frame of the stream into `frame` (true), feeding packets as needed; false at the end.
[[nodiscard]] Result<bool>
pumpFrame(AVFormatContext &format, int streamIndex, AVPacket &packet, AVCodecContext &codec, AVFrame &frame);

} // namespace cc::media
