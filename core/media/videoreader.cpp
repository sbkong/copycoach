// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/ffmpegutil.h"
#include "core/media/videofile.h"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace cc::media
{

namespace
{

constexpr double forwardWindowSeconds = 1.0; // targets this far ahead decode on instead of seeking
constexpr int rgbBytesPerPixel = 3;

double ptsSeconds(const AVFrame &frame, AVRational timeBase)
{
    const std::int64_t pts = frame.best_effort_timestamp != AV_NOPTS_VALUE ? frame.best_effort_timestamp : frame.pts;
    return pts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(pts) * av_q2d(timeBase);
}

} // namespace

struct VideoReader::Impl {
    FormatPtr format;
    CodecPtr codec;
    SwsPtr sws;
    PacketPtr packet{av_packet_alloc()};
    FramePtr frame{av_frame_alloc()};
    VideoInfo info;
    int stream = -1;
    AVRational timeBase{1, 1};
    bool atEnd = false;
    std::optional<VideoFrame> current; // the frame last returned
    std::optional<VideoFrame> pending; // the one after it, decoded past the target
    std::optional<double> lastTarget;

    [[nodiscard]] Result<std::optional<VideoFrame>> nextFrame();
    [[nodiscard]] Result<VideoFrame> toRgb(const AVFrame &src) const;
    [[nodiscard]] Result<void> seekBefore(double seconds);
    [[nodiscard]] Result<VideoFrame> decodeUpTo(double seconds);
    void reset();
};

/// The next decoded frame in stream order, or nullopt at the end.
Result<std::optional<VideoFrame>> VideoReader::Impl::nextFrame()
{
    if (atEnd) {
        return std::nullopt;
    }
    const Result<bool> got = pumpFrame(*format, stream, *packet, *codec, *frame);
    if (!got) {
        return std::unexpected(got.error());
    }
    if (!*got) {
        atEnd = true;
        return std::nullopt;
    }
    Result<VideoFrame> out = toRgb(*frame);
    av_frame_unref(frame.get());
    if (!out) {
        return std::unexpected(out.error());
    }
    return std::move(*out);
}

Result<VideoFrame> VideoReader::Impl::toRgb(const AVFrame &src) const
{
    SwsContext *ctx =
        sws_getCachedContext(const_cast<SwsContext *>(sws.get()), // NOLINT(cppcoreguidelines-pro-type-const-cast)
                             src.width,
                             src.height,
                             static_cast<AVPixelFormat>(src.format),
                             src.width,
                             src.height,
                             AV_PIX_FMT_RGB24,
                             SWS_BILINEAR,
                             nullptr,
                             nullptr,
                             nullptr);
    if (ctx == nullptr) {
        return fail(ErrorCode::Unsupported, urlOf(*format), "pixel format");
    }
    if (ctx != sws.get()) {
        const_cast<Impl *>(this)->sws.reset(ctx); // NOLINT(cppcoreguidelines-pro-type-const-cast) cache
    }
    VideoFrame out;
    out.seconds = ptsSeconds(src, timeBase);
    out.width = src.width;
    out.height = src.height;
    out.rgb.resize(static_cast<std::size_t>(src.width) * static_cast<std::size_t>(src.height) * rgbBytesPerPixel);
    std::array<std::uint8_t *, 1> dst{out.rgb.data()};
    const std::array<int, 1> stride{src.width * rgbBytesPerPixel};
    sws_scale(ctx, src.data, src.linesize, 0, src.height, dst.data(), stride.data()); // NOLINT
    return out;
}

Result<void> VideoReader::Impl::seekBefore(double seconds)
{
    const auto target = static_cast<std::int64_t>(std::llround(std::max(0.0, seconds) / av_q2d(timeBase)));
    if (const int rc = av_seek_frame(format.get(), stream, target, AVSEEK_FLAG_BACKWARD); rc < 0) {
        return fail(ErrorCode::FileCorrupt, urlOf(*format), ffmpegError(rc));
    }
    avcodec_flush_buffers(codec.get());
    atEnd = false;
    current.reset();
    pending.reset();
    return {};
}

/// Frames forward until one lies past the target; the last one at or before it is the answer and the one
/// past it waits as `pending` for the next call.
Result<VideoFrame> VideoReader::Impl::decodeUpTo(double seconds)
{
    if (pending && current && pending->seconds > seconds) {
        return *current; // the target moved less than a frame
    }
    std::optional<VideoFrame> candidate = std::exchange(pending, std::nullopt);
    while (true) {
        Result<std::optional<VideoFrame>> next = nextFrame();
        if (!next) {
            return std::unexpected(next.error());
        }
        if (!*next) {
            break;
        }
        const bool past = (*next)->seconds > seconds;
        if (past && candidate) {
            pending = std::move(**next);
            break;
        }
        candidate = std::move(**next); // also the first frame when the target lies before it
        if (past) {
            break;
        }
    }
    if (!candidate) {
        return fail(ErrorCode::FileCorrupt, urlOf(*format), "no frame");
    }
    current = *candidate;
    return *candidate;
}

void VideoReader::Impl::reset()
{
    codec.reset();
    format.reset();
    sws.reset();
    current.reset();
    pending.reset();
    lastTarget.reset();
    atEnd = false;
    stream = -1;
}

VideoReader::VideoReader()
    : m_impl(std::make_unique<Impl>())
{
}

VideoReader::~VideoReader() = default;

Result<void> VideoReader::open(const std::filesystem::path &path)
{
    close();
    Result<VideoInfo> info = probeVideo(path);
    if (!info) {
        return std::unexpected(info.error());
    }
    if (Result<void> r = openFormat(path, m_impl->format); !r) {
        return r;
    }
    m_impl->info = *info;
    m_impl->stream = av_find_best_stream(m_impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (Result<void> r = openDecoder(*m_impl->format, m_impl->stream, m_impl->codec); !r) {
        close();
        return r;
    }
    m_impl->timeBase = m_impl->format->streams[m_impl->stream]->time_base; // NOLINT
    return {};
}

void VideoReader::close()
{
    m_impl->reset();
}

bool VideoReader::isOpen() const
{
    return m_impl->codec != nullptr;
}

const VideoInfo &VideoReader::info() const
{
    return m_impl->info;
}

Result<VideoFrame> VideoReader::frameAt(double seconds)
{
    if (!isOpen()) {
        return fail(ErrorCode::FileNotFound, "video");
    }
    const bool forward =
        m_impl->lastTarget && seconds >= *m_impl->lastTarget && seconds - *m_impl->lastTarget < forwardWindowSeconds;
    if (!forward) {
        if (Result<void> r = m_impl->seekBefore(seconds); !r) {
            return std::unexpected(r.error());
        }
    }
    Result<VideoFrame> frame = m_impl->decodeUpTo(seconds);
    if (frame) {
        m_impl->lastTarget = seconds;
    }
    return frame;
}

} // namespace cc::media
