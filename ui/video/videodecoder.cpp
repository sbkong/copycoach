// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/video/videodecoder.h"

#include "ui/shell/errortext.h"

#include <algorithm>
#include <span>

namespace cc::ui
{

VideoDecoder::VideoDecoder(QObject *parent)
    : QObject(parent)
{
}

void VideoDecoder::request(double seconds)
{
    m_wanted.store(seconds);
    m_generation.fetch_add(1);
    QMetaObject::invokeMethod(this, &VideoDecoder::decodeLatest, Qt::QueuedConnection);
}

void VideoDecoder::openFile(const QString &path)
{
    const Result<void> r = m_reader.open(std::filesystem::path(path.toStdWString()));
    if (!r) {
        emit failed(errorText(r.error()));
        return;
    }
    m_decoded = -1;
    const media::VideoInfo &info = m_reader.info();
    emit opened(info.width, info.height, info.fps, info.seconds, info.audioStreams);
}

void VideoDecoder::closeFile()
{
    m_reader.close();
}

/// Several queued calls after a burst of requests collapse into one decode of the newest time.
void VideoDecoder::decodeLatest()
{
    const int generation = m_generation.load();
    if (generation == m_decoded || !m_reader.isOpen()) {
        return;
    }
    m_decoded = generation;
    const double seconds = m_wanted.load();
    const Result<media::VideoFrame> frame = m_reader.frameAt(seconds);
    if (!frame) {
        emit failed(errorText(frame.error()));
        return;
    }
    QImage image(frame->width, frame->height, QImage::Format_RGB888);
    const auto rowBytes = static_cast<std::size_t>(frame->width) * 3;
    const std::span<const std::uint8_t> rgb(frame->rgb);
    for (int y = 0; y < frame->height; ++y) {
        const std::span<const std::uint8_t> row = rgb.subspan(static_cast<std::size_t>(y) * rowBytes, rowBytes);
        std::ranges::copy(row, image.scanLine(y));
    }
    emit frameReady(image, frame->seconds);
}

} // namespace cc::ui
