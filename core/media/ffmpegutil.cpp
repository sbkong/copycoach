// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/ffmpegutil.h"

extern "C" {
#include <libavutil/error.h>
}

#include <array>

namespace cc::media
{

std::string ffmpegError(int code)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(code, text.data(), text.size());
    return {text.data()};
}

std::string urlOf(const AVFormatContext &format)
{
    return format.url != nullptr ? std::string(format.url) : std::string();
}

Result<void> openFormat(const std::filesystem::path &path, FormatPtr &format)
{
    if (!std::filesystem::exists(path)) {
        return fail(ErrorCode::FileNotFound, path.string());
    }
    AVFormatContext *ctx = nullptr;
    const std::u8string u8 = path.u8string();
    const std::string utf8(u8.begin(), u8.end()); // FFmpeg on Windows takes UTF-8 paths
    int rc = avformat_open_input(&ctx, utf8.c_str(), nullptr, nullptr);
    if (rc < 0) {
        return fail(ErrorCode::FileCorrupt, path.string(), ffmpegError(rc));
    }
    format.reset(ctx);
    rc = avformat_find_stream_info(format.get(), nullptr);
    if (rc < 0) {
        return fail(ErrorCode::FileCorrupt, path.string(), ffmpegError(rc));
    }
    return {};
}

Result<void> openDecoder(const AVFormatContext &format, int streamIndex, CodecPtr &codec)
{
    const AVCodecParameters *par = format.streams[streamIndex]->codecpar; // NOLINT
    const AVCodec *decoder = avcodec_find_decoder(par->codec_id);
    if (decoder == nullptr) {
        return fail(ErrorCode::Unsupported, urlOf(format), avcodec_get_name(par->codec_id));
    }
    codec.reset(avcodec_alloc_context3(decoder));
    if (!codec || avcodec_parameters_to_context(codec.get(), par) < 0) {
        return fail(ErrorCode::Unsupported, urlOf(format), "codec parameters");
    }
    codec->pkt_timebase = format.streams[streamIndex]->time_base; // NOLINT
    if (const int rc = avcodec_open2(codec.get(), decoder, nullptr); rc < 0) {
        return fail(ErrorCode::Unsupported, urlOf(format), ffmpegError(rc));
    }
    return {};
}

Result<Receive> receiveFrame(AVCodecContext &codec, AVFrame &frame, const std::string &subject)
{
    const int got = avcodec_receive_frame(&codec, &frame);
    if (got >= 0) {
        return Receive::Frame;
    }
    if (got == AVERROR(EAGAIN)) {
        return Receive::NeedPacket;
    }
    if (got == AVERROR_EOF) {
        return Receive::End;
    }
    return fail(ErrorCode::FileCorrupt, subject, ffmpegError(got));
}

Result<void> feedPacket(AVFormatContext &format, int streamIndex, AVPacket &packet, AVCodecContext &codec)
{
    int read = av_read_frame(&format, &packet);
    while (read >= 0 && packet.stream_index != streamIndex) {
        av_packet_unref(&packet);
        read = av_read_frame(&format, &packet);
    }
    if (read < 0 && read != AVERROR_EOF) {
        return fail(ErrorCode::FileCorrupt, urlOf(format), ffmpegError(read));
    }
    const int sent = avcodec_send_packet(&codec, read >= 0 ? &packet : nullptr);
    av_packet_unref(&packet);
    if (sent < 0 && sent != AVERROR_EOF) {
        return fail(ErrorCode::FileCorrupt, urlOf(format), ffmpegError(sent));
    }
    return {};
}

Result<bool>
pumpFrame(AVFormatContext &format, int streamIndex, AVPacket &packet, AVCodecContext &codec, AVFrame &frame)
{
    const std::string subject = urlOf(format);
    while (true) {
        const Result<Receive> got = receiveFrame(codec, frame, subject);
        if (!got) {
            return std::unexpected(got.error());
        }
        if (*got != Receive::NeedPacket) {
            return *got == Receive::Frame;
        }
        if (Result<void> fed = feedPacket(format, streamIndex, packet, codec); !fed) {
            return std::unexpected(fed.error());
        }
    }
}

} // namespace cc::media
