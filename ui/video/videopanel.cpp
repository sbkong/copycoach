// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/video/videopanel.h"

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/video/videodecoder.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace cc::ui
{

namespace
{

constexpr int headerSpacingPx = 4;
constexpr double defaultFps = 30.0; // frame steps before the file reports its rate

QToolButton *tool(const QIcon &icon, const QString &text, bool checkable, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setAutoRaise(true);
    button->setCheckable(checkable);
    button->setFocusPolicy(Qt::NoFocus); // keys stay with the panel
    return button;
}

} // namespace

VideoView::VideoView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_theme->color(theme::Token::BackgroundCanvasColor));
    setPalette(pal);
    setMinimumHeight(1);
}

void VideoView::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void VideoView::setMirrored(bool mirrored)
{
    m_mirrored = mirrored;
    update();
}

bool VideoView::mirrored() const
{
    return m_mirrored;
}

const QImage &VideoView::image() const
{
    return m_image;
}

/// Aspect-fit, centred; mirrored by flipping x around the middle.
void VideoView::paintEvent(QPaintEvent * /*event*/)
{
    if (m_image.isNull()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSize fitted = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    if (m_mirrored) {
        painter.translate(width(), 0);
        painter.scale(-1.0, 1.0);
        painter.drawImage(QRect(width() - target.right() - 1, target.top(), fitted.width(), fitted.height()), m_image);
        return;
    }
    painter.drawImage(target, m_image);
}

VideoPanel::VideoPanel(const Theme &theme, const Icons &icons, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_decoder(new VideoDecoder())
    , m_view(new VideoView(theme, this))
    , m_mirror(tool(icons.icon(QStringLiteral("flip-horizontal-2")), tr("좌우 반전"), true, this))
    , m_prev(tool(icons.icon(QStringLiteral("chevron-left")), tr("이전 프레임"), false, this))
    , m_next(tool(icons.icon(QStringLiteral("chevron-right")), tr("다음 프레임"), false, this))
    , m_audioTrack(new QComboBox(this))
{
    setAccessibleName(tr("영상"));
    setFocusPolicy(Qt::StrongFocus);
    m_audioTrack->setAccessibleName(tr("오디오 트랙"));
    m_audioTrack->setToolTip(tr("오디오 트랙"));
    m_audioTrack->setFocusPolicy(Qt::NoFocus);
    m_audioTrack->hide();
    auto *header = new QHBoxLayout();
    header->setContentsMargins(headerSpacingPx, headerSpacingPx, headerSpacingPx, 0);
    header->setSpacing(headerSpacingPx);
    header->addWidget(m_mirror);
    header->addWidget(m_prev);
    header->addWidget(m_next);
    header->addStretch(1);
    header->addWidget(m_audioTrack);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(headerSpacingPx);
    layout->addLayout(header);
    layout->addWidget(m_view, 1);

    m_decoder->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_decoder, &QObject::deleteLater);
    connect(m_decoder, &VideoDecoder::opened, this, &VideoPanel::onOpened);
    connect(m_decoder, &VideoDecoder::frameReady, this, &VideoPanel::onFrame);
    connect(m_decoder, &VideoDecoder::failed, this, &VideoPanel::failed);
    connect(m_mirror, &QToolButton::toggled, m_view, &VideoView::setMirrored);
    connect(m_prev, &QToolButton::clicked, this, [this] {
        stepFrame(-1);
    });
    connect(m_next, &QToolButton::clicked, this, [this] {
        stepFrame(1);
    });
    connect(m_audioTrack, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && m_hasVideo) {
            emit audioTrackChosen(index);
        }
    });
    m_thread.start();
}

VideoPanel::~VideoPanel()
{
    m_thread.quit();
    m_thread.wait();
}

void VideoPanel::setVideo(const std::filesystem::path &path, int audioTrack)
{
    m_hasVideo = true;
    m_shown.reset();
    m_view->setImage(QImage());
    {
        const QSignalBlocker blocker(m_audioTrack);
        m_audioTrack->clear();
        m_audioTrack->addItem(tr("트랙 %1").arg(audioTrack + 1)); // replaced once the file says how many
    }
    QMetaObject::invokeMethod(m_decoder, [decoder = m_decoder, file = QString::fromStdWString(path.wstring())] {
        decoder->openFile(file);
    });
    m_decoder->request(m_wanted);
}

void VideoPanel::clearVideo()
{
    m_hasVideo = false;
    m_shown.reset();
    m_view->setImage(QImage());
    m_audioTrack->hide();
    QMetaObject::invokeMethod(m_decoder, &VideoDecoder::closeFile);
}

bool VideoPanel::hasVideo() const
{
    return m_hasVideo;
}

void VideoPanel::showAt(double seconds)
{
    m_wanted = seconds;
    if (m_hasVideo) {
        m_decoder->request(seconds);
    }
}

/// Half a frame past the next frame's start, so rounding lands inside it; the transport follows.
void VideoPanel::stepFrame(int delta)
{
    if (!m_hasVideo) {
        return;
    }
    const double fps = m_fps > 0.0 ? m_fps : defaultFps;
    const double from = m_shown.value_or(m_wanted);
    const double frame = std::round(from * fps) + delta;
    const double seconds = std::max(0.0, (frame + 0.5) / fps);
    showAt(seconds);
    emit seekRequested(seconds);
}

void VideoPanel::setMirrored(bool mirrored)
{
    m_mirror->setChecked(mirrored);
}

bool VideoPanel::mirrored() const
{
    return m_view->mirrored();
}

std::optional<double> VideoPanel::shownSeconds() const
{
    return m_shown;
}

const QImage &VideoPanel::currentFrame() const
{
    return m_view->image();
}

double VideoPanel::fps() const
{
    return m_fps;
}

void VideoPanel::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    emit focusChanged(true);
}

void VideoPanel::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    emit focusChanged(false);
}

/// The track combo appears only when there is a choice (video.md section 2).
void VideoPanel::onOpened(int /*width*/, int /*height*/, double fps, double /*seconds*/, int audioStreams)
{
    m_fps = fps;
    const int chosen = std::max(0, m_audioTrack->currentIndex());
    const QSignalBlocker blocker(m_audioTrack);
    m_audioTrack->clear();
    for (int i = 0; i < audioStreams; ++i) {
        m_audioTrack->addItem(tr("트랙 %1").arg(i + 1));
    }
    m_audioTrack->setCurrentIndex(std::min(chosen, audioStreams - 1));
    m_audioTrack->setVisible(audioStreams > 1);
}

void VideoPanel::onFrame(const QImage &image, double seconds)
{
    if (!m_hasVideo) {
        return;
    }
    m_shown = seconds;
    m_view->setImage(image);
}

} // namespace cc::ui
