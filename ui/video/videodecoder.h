// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/media/videofile.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <filesystem>

// The video panel's decoder on its own thread (video.md section 5): the panel asks for a time, the decoder
// answers with the frame for the newest time asked, dropping the ones that piled up while it worked (frames
// follow the audio clock, drops allowed). Everything but request() runs on the decoder's thread.
namespace cc::ui
{

class VideoDecoder final : public QObject
{
    Q_OBJECT
public:
    explicit VideoDecoder(QObject *parent = nullptr);

    /// Thread-safe: remembers the time and queues a decode; only the latest time is decoded.
    void request(double seconds);

    Q_INVOKABLE void openFile(const QString &path); // then opened() or failed()
    Q_INVOKABLE void closeFile();
    Q_INVOKABLE void decodeLatest();

signals:
    void opened(int width, int height, double fps, double seconds, int audioStreams);
    void frameReady(const QImage &image, double seconds);
    void failed(const QString &message);

private:
    media::VideoReader m_reader;
    std::atomic<double> m_wanted{0.0};
    std::atomic<int> m_generation{0};
    int m_decoded = -1;
};

} // namespace cc::ui
