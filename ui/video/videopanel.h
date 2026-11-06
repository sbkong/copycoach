// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QImage>
#include <QThread>
#include <QWidget>

#include <filesystem>
#include <optional>

class QComboBox;
class QToolButton;

namespace cc::ui
{

class Icons;
class Theme;
class VideoDecoder;

/// The picture itself: the frame fitted into the widget, mirrored when asked (a drawing transform).
class VideoView final : public QWidget
{
    Q_OBJECT
public:
    explicit VideoView(const Theme &theme, QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setMirrored(bool mirrored);
    [[nodiscard]] bool mirrored() const;
    [[nodiscard]] const QImage &image() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const Theme *m_theme;
    QImage m_image;
    bool m_mirrored = false;
};

/// The video panel (video.md sections 2 to 5, dock.video): the frame at the audio clock's time, a header row
/// with mirror, frame step and the audio track choice. Decoding runs on the panel's own thread; the time
/// shown is whatever the clock last said. Hidden by the shell for projects without video.
class VideoPanel final : public QWidget
{
    Q_OBJECT
public:
    VideoPanel(const Theme &theme, const Icons &icons, QWidget *parent = nullptr);
    ~VideoPanel() override;
    Q_DISABLE_COPY_MOVE(VideoPanel)

    void setVideo(const std::filesystem::path &path, int audioTrack);
    void clearVideo();
    [[nodiscard]] bool hasVideo() const;
    /// The audio clock moved: show the frame for this time (coalesced on the decoder thread).
    void showAt(double seconds);
    /// One frame on from the one shown (or back); asks the transport to follow through seekRequested().
    void stepFrame(int delta);
    void setMirrored(bool mirrored);
    [[nodiscard]] bool mirrored() const;
    [[nodiscard]] std::optional<double> shownSeconds() const;
    [[nodiscard]] const QImage &currentFrame() const;
    [[nodiscard]] double fps() const;

signals:
    void seekRequested(double seconds);
    void audioTrackChosen(int track);
    void focusChanged(bool focused); // the video context follows it
    void failed(const QString &message);

protected:
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void onOpened(int width, int height, double fps, double seconds, int audioStreams);
    void onFrame(const QImage &image, double seconds);

    const Theme *m_theme;
    QThread m_thread;
    VideoDecoder *m_decoder; // lives on m_thread
    VideoView *m_view;
    QToolButton *m_mirror;
    QToolButton *m_prev;
    QToolButton *m_next;
    QComboBox *m_audioTrack;
    bool m_hasVideo = false;
    double m_fps = 0.0;
    double m_wanted = 0.0;
    std::optional<double> m_shown;
};

} // namespace cc::ui
