// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/media/videofile.h"

#include "core/media/audiofile.h"
#include "core/media/ffmpegutil.h"

extern "C" {
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <array>
#include <string>

namespace cc::media
{

namespace
{

constexpr int maxExtractChannels = 2;

Result<void> readInfo(AVFormatContext &format, VideoInfo &info)
{
    const int video = av_find_best_stream(&format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video < 0) {
        return fail(ErrorCode::Unsupported, urlOf(format), "no video stream");
    }
    const AVStream &stream = *format.streams[video]; // NOLINT
    info.width = stream.codecpar->width;
    info.height = stream.codecpar->height;
    const AVRational rate = av_guess_frame_rate(&format, format.streams[video], nullptr); // NOLINT
    info.fps = rate.den > 0 ? av_q2d(rate) : 0.0;
    info.seconds = format.duration > 0 ? static_cast<double>(format.duration) / AV_TIME_BASE : 0.0;
    info.audioStreams = 0;
    for (unsigned i = 0; i < format.nb_streams; ++i) {
        if (format.streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { // NOLINT
            ++info.audioStreams;
        }
    }
    return {};
}

/// The index of the n-th audio stream, or -1.
int audioStreamIndex(const AVFormatContext &format, int audioTrack)
{
    int seen = 0;
    for (unsigned i = 0; i < format.nb_streams; ++i) {
        if (format.streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && seen++ == audioTrack) { // NOLINT
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Decoded audio frames to interleaved float, at most two channels, through one resampler context.
class AudioConverter
{
public:
    [[nodiscard]] Result<void> open(const AVCodecContext &codec)
    {
        m_channels = std::min(maxExtractChannels, codec.ch_layout.nb_channels);
        av_channel_layout_default(&m_layout, m_channels);
        SwrContext *ctx = nullptr;
        const int rc = swr_alloc_set_opts2(&ctx,
                                           &m_layout,
                                           AV_SAMPLE_FMT_FLT,
                                           codec.sample_rate,
                                           &codec.ch_layout,
                                           codec.sample_fmt,
                                           codec.sample_rate,
                                           0,
                                           nullptr);
        m_swr.reset(ctx);
        if (rc < 0 || swr_init(m_swr.get()) < 0) {
            return fail(ErrorCode::Unsupported, "swresample", "cannot convert this audio layout");
        }
        return {};
    }

    [[nodiscard]] int channels() const
    {
        return m_channels;
    }

    /// The frame's samples, interleaved, into `out` (replacing its contents).
    [[nodiscard]] Result<void> convert(const AVFrame &frame, std::vector<float> &out)
    {
        const int capacity = frame.nb_samples + static_cast<int>(swr_get_delay(m_swr.get(), frame.sample_rate));
        out.resize(static_cast<std::size_t>(capacity) * static_cast<std::size_t>(m_channels));
        std::array<std::uint8_t *, 1> dst{reinterpret_cast<std::uint8_t *>(out.data())}; // NOLINT
        const int got = swr_convert(m_swr.get(), dst.data(), capacity, frame.extended_data, frame.nb_samples);
        if (got < 0) {
            return fail(ErrorCode::FileCorrupt, "swresample", ffmpegError(got));
        }
        out.resize(static_cast<std::size_t>(got) * static_cast<std::size_t>(m_channels));
        return {};
    }

private:
    SwrPtr m_swr;
    AVChannelLayout m_layout{};
    int m_channels = 0;
};

/// The n-th audio stream of the file with its decoder open.
struct AudioStream {
    FormatPtr format;
    CodecPtr codec;
    int index = -1;

    [[nodiscard]] Result<void> open(const std::filesystem::path &video, int audioTrack)
    {
        if (Result<void> r = openFormat(video, format); !r) {
            return r;
        }
        index = audioStreamIndex(*format, audioTrack);
        if (index < 0) {
            return fail(ErrorCode::Unsupported, video.string(), "no such audio stream");
        }
        return openDecoder(*format, index, codec);
    }

    [[nodiscard]] double seconds(const AVFrame &frame) const
    {
        const AVRational timeBase = format->streams[index]->time_base; // NOLINT
        return frame.pts == AV_NOPTS_VALUE ? -1.0 : static_cast<double>(frame.pts) * av_q2d(timeBase);
    }

    [[nodiscard]] double duration() const
    {
        return format->duration > 0 ? static_cast<double>(format->duration) / AV_TIME_BASE : 0.0;
    }
};

/// Every decoded frame of the stream to `onFrame`; Cancelled between packets.
template<typename OnFrame>
Result<void> decodeStream(AudioStream &stream, const std::stop_token &stop, const OnFrame &onFrame)
{
    const PacketPtr packet(av_packet_alloc());
    const FramePtr frame(av_frame_alloc());
    while (true) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled, urlOf(*stream.format));
        }
        const Result<bool> got = pumpFrame(*stream.format, stream.index, *packet, *stream.codec, *frame);
        if (!got) {
            return std::unexpected(got.error());
        }
        if (!*got) {
            return {};
        }
        const Result<void> r = onFrame(*frame);
        av_frame_unref(frame.get());
        if (!r) {
            return r;
        }
    }
}

} // namespace

Result<VideoInfo> probeVideo(const std::filesystem::path &path)
{
    FormatPtr format;
    if (Result<void> r = openFormat(path, format); !r) {
        return std::unexpected(r.error());
    }
    VideoInfo info;
    if (Result<void> r = readInfo(*format, info); !r) {
        return std::unexpected(r.error());
    }
    return info;
}

Result<void> extractAudio(const std::filesystem::path &video,
                          int audioTrack,
                          const std::filesystem::path &flac,
                          const std::stop_token &stop,
                          const jobs::Report &report)
{
    AudioStream stream;
    if (Result<void> r = stream.open(video, audioTrack); !r) {
        return r;
    }
    AudioConverter converter;
    if (Result<void> r = converter.open(*stream.codec); !r) {
        return r;
    }
    AudioFileWriter writer;
    if (Result<void> r = writer.open(flac, stream.codec->sample_rate, converter.channels(), AudioFileFormat::Flac16);
        !r) {
        return r;
    }
    const double total = stream.duration();
    std::vector<float> samples;
    const Result<void> decoded = decodeStream(stream, stop, [&](const AVFrame &frame) -> Result<void> {
        if (Result<void> r = converter.convert(frame, samples); !r) {
            return r;
        }
        if (total > 0.0 && stream.seconds(frame) >= 0.0) {
            report(std::clamp(stream.seconds(frame) / total, 0.0, 1.0));
        }
        return writer.write(samples);
    });
    if (!decoded) {
        static_cast<void>(writer.close());
        std::error_code ec;
        std::filesystem::remove(flac, ec);
        return decoded;
    }
    report(1.0);
    return writer.close();
}

} // namespace cc::media
