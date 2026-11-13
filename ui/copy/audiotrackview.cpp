// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/audiotrackview.h"

#include "ui/theme/theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace cc::ui
{

namespace
{

constexpr double mapRate = 1000.0; // the TimeMap counts milliseconds here
constexpr double selectionAlpha = 0.25;
constexpr int playheadWidthPx = 2;
constexpr int syncTriangleHalf = 5;
constexpr int syncTriangleHeight = 8;
constexpr int barNumberInset = 2;
constexpr double scrollUnitsPerSecond = 1000.0; // the scroll bar counts milliseconds
constexpr double wheelZoomStep = 1.25; // per notch with Ctrl
constexpr double wheelScrollFraction = 0.1; // of the visible span per notch
constexpr double playheadLeadFraction = 0.1; // where the playhead lands after the view jumps to it

} // namespace

AudioTrackView::AudioTrackView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_scrollBar(new QScrollBar(Qt::Horizontal, this))
{
    setAccessibleName(tr("오디오 트랙"));
    setMinimumHeight(preferredHeight);
    setMouseTracking(false);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_theme->color(theme::Token::BackgroundCanvasColor));
    setPalette(pal);
    m_scrollBar->hide(); // only when zoomed in
    m_scrollBar->setAccessibleName(tr("오디오 트랙 가로 스크롤"));
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollSeconds = value / scrollUnitsPerSecond;
        update();
    });
}

void AudioTrackView::setPeaks(std::optional<media::Peaks> peaks)
{
    m_peaks = std::move(peaks);
    syncScrollBar();
    update();
}

void AudioTrackView::setTiming(const model::Score &score, std::vector<model::SyncPoint> points)
{
    m_score = score;
    m_points = std::move(points);
    m_map = std::make_unique<model::TimeMap>(model::TimeMap::fromSyncPoints(score, m_points, mapRate));
    update();
}

void AudioTrackView::clearTiming()
{
    m_score.reset();
    m_points.clear();
    m_map.reset();
    update();
}

void AudioTrackView::setPredictedDownbeats(std::vector<double> seconds)
{
    m_predicted = std::move(seconds);
    update();
}

void AudioTrackView::setPredictionVisible(bool visible)
{
    m_predictionVisible = visible;
    update();
}

bool AudioTrackView::predictionVisible() const
{
    return m_predictionVisible;
}

void AudioTrackView::setPlayheadSeconds(std::optional<double> seconds)
{
    if (m_playhead == seconds) {
        return;
    }
    m_playhead = seconds;
    keepPlayheadVisible();
    update();
}

// --- zoom and scroll ---------------------------------------------------------------------------------------

void AudioTrackView::setZoom(double factor)
{
    const double zoom = std::clamp(factor, 1.0, maxZoom);
    if (zoom == m_zoom) {
        return;
    }
    m_zoom = zoom;
    setScrollSeconds(m_scrollSeconds); // clamps to the new span and refreshes the bar
}

void AudioTrackView::zoomBy(double factor, int aroundX)
{
    const double anchor = secondsAt(aroundX);
    const double zoom = std::clamp(m_zoom * factor, 1.0, maxZoom);
    if (zoom == m_zoom) {
        return;
    }
    m_zoom = zoom;
    const double fraction = width() > 0 ? static_cast<double>(aroundX) / width() : 0.0;
    setScrollSeconds(anchor - (fraction * visibleSeconds()));
}

double AudioTrackView::zoom() const
{
    return m_zoom;
}

void AudioTrackView::setScrollSeconds(double seconds)
{
    const double most = std::max(0.0, durationSeconds() - visibleSeconds());
    m_scrollSeconds = std::clamp(seconds, 0.0, most);
    syncScrollBar();
    update();
}

double AudioTrackView::scrollSeconds() const
{
    return m_scrollSeconds;
}

double AudioTrackView::visibleSeconds() const
{
    return durationSeconds() / m_zoom;
}

int AudioTrackView::trackHeight() const
{
    return m_scrollBar->isVisible() ? height() - m_scrollBar->height() : height();
}

void AudioTrackView::layoutScrollBar()
{
    const int barHeight = m_scrollBar->sizeHint().height();
    m_scrollBar->setGeometry(0, height() - barHeight, width(), barHeight);
}

/// The bar spans the file in milliseconds with the visible span as its page; hidden at zoom 1.
void AudioTrackView::syncScrollBar()
{
    const bool zoomed = m_zoom > 1.0 && durationSeconds() > 0.0;
    if (zoomed != m_scrollBar->isVisible()) {
        m_scrollBar->setVisible(zoomed);
        layoutScrollBar();
    }
    const QSignalBlocker quiet(m_scrollBar);
    const double most = std::max(0.0, durationSeconds() - visibleSeconds());
    m_scrollBar->setRange(0, static_cast<int>(std::lround(most * scrollUnitsPerSecond)));
    m_scrollBar->setPageStep(static_cast<int>(std::lround(visibleSeconds() * scrollUnitsPerSecond)));
    m_scrollBar->setSingleStep(std::max(1, m_scrollBar->pageStep() / 10));
    m_scrollBar->setValue(static_cast<int>(std::lround(m_scrollSeconds * scrollUnitsPerSecond)));
}

/// Zoomed in, a playhead that left the view jumps it so the playhead sits near the left edge (DAW style).
void AudioTrackView::keepPlayheadVisible()
{
    if (!m_playhead || m_zoom <= 1.0) {
        return;
    }
    const double visible = visibleSeconds();
    if (*m_playhead >= m_scrollSeconds && *m_playhead < m_scrollSeconds + visible) {
        return;
    }
    setScrollSeconds(*m_playhead - (playheadLeadFraction * visible));
}

void AudioTrackView::setSelectedMeasures(std::optional<std::pair<std::size_t, std::size_t>> range)
{
    m_selection = std::move(range);
    update();
}

std::optional<std::pair<std::size_t, std::size_t>> AudioTrackView::selectedMeasures() const
{
    return m_selection;
}

double AudioTrackView::durationSeconds() const
{
    if (m_peaks && m_peaks->seconds > 0.0) {
        return m_peaks->seconds;
    }
    if (m_map && m_score && !m_score->measures.empty()) {
        return m_map->tickToSeconds(m_score->totalTicks());
    }
    return 0.0;
}

double AudioTrackView::secondsAt(int x) const
{
    const double duration = durationSeconds();
    if (duration <= 0.0 || width() <= 0) {
        return 0.0;
    }
    const double seconds = m_scrollSeconds + (static_cast<double>(x) / width() * visibleSeconds());
    return std::clamp(seconds, 0.0, duration);
}

int AudioTrackView::xAt(double seconds) const
{
    const double visible = visibleSeconds();
    if (visible <= 0.0) {
        return 0;
    }
    return static_cast<int>(std::lround((seconds - m_scrollSeconds) / visible * width()));
}

/// The measure whose span (by the map) contains the time at x.
std::optional<std::size_t> AudioTrackView::measureAt(int x) const
{
    if (!m_map || !m_score || m_score->measures.empty()) {
        return std::nullopt;
    }
    const model::Tick tick = m_map->secondsToTick(secondsAt(x));
    for (std::size_t m = 0; m < m_score->measures.size(); ++m) {
        if (tick < m_score->measureStartTick(m + 1)) {
            return m;
        }
    }
    return m_score->measures.size() - 1;
}

QSize AudioTrackView::sizeHint() const
{
    return {400, preferredHeight};
}

// --- painting ----------------------------------------------------------------------------------------------

void AudioTrackView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(QRect(0, 0, width(), trackHeight()));
    paintWaveform(painter);
    paintGrid(painter);
    paintSyncPoints(painter);
    paintPrediction(painter);
    paintSelectionAndPlayhead(painter);
}

void AudioTrackView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutScrollBar();
    syncScrollBar();
}

/// Ctrl + wheel zooms around the pointer; a plain or horizontal wheel scrolls when zoomed in.
void AudioTrackView::wheelEvent(QWheelEvent *event)
{
    const QPoint delta = event->angleDelta();
    const int notches = (delta.x() != 0 ? delta.x() : delta.y()) / QWheelEvent::DefaultDeltasPerStep;
    if (notches == 0) {
        return;
    }
    event->accept();
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
        zoomBy(notches > 0 ? wheelZoomStep : 1.0 / wheelZoomStep, static_cast<int>(event->position().x()));
        return;
    }
    if (m_zoom > 1.0) {
        setScrollSeconds(m_scrollSeconds - (notches * wheelScrollFraction * visibleSeconds()));
    }
}

/// Predicted bar starts: dashed in the prediction colour (copy-spec.md section 16.2 C-6) with their number
/// below the real bar numbers, so the grey grid reads as a proposal next to the solid one.
void AudioTrackView::paintPrediction(QPainter &painter) const
{
    if (!m_predictionVisible || m_predicted.empty() || durationSeconds() <= 0.0) {
        return;
    }
    QPen pen(m_theme->color(theme::Token::PredictionColor), 1);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setFont(m_theme->font(theme::TypeStyle::Caption, false));
    const int textY = (2 * painter.fontMetrics().ascent()) + (2 * barNumberInset);
    for (std::size_t k = 0; k < m_predicted.size(); ++k) {
        const int x = xAt(m_predicted[k]);
        painter.drawLine(x, 0, x, trackHeight());
        painter.drawText(QPoint(x + barNumberInset, textY), QString::number(k + 1));
    }
}

/// One mirrored column per pixel: the peak of the buckets that fall into it.
void AudioTrackView::paintWaveform(QPainter &painter) const
{
    if (!m_peaks || m_peaks->buckets.empty()) {
        painter.setPen(m_theme->color(theme::Token::ContentColorSecondary));
        painter.setFont(m_theme->font(theme::TypeStyle::Body));
        painter.drawText(QRect(0, 0, width(), trackHeight()), Qt::AlignCenter, tr("음원이 없습니다"));
        return;
    }
    const double mid = trackHeight() / 2.0;
    const double half = mid - barNumberInset;
    // buckets per pixel at this zoom, offset by the scrolled-away buckets
    const double perPixel = static_cast<double>(m_peaks->buckets.size()) / std::max(1, width()) / m_zoom;
    const double first =
        m_scrollSeconds / std::max(1e-9, m_peaks->seconds) * static_cast<double>(m_peaks->buckets.size());
    const auto peakAt = [&](int x) {
        const auto from = static_cast<std::size_t>(first + (x * perPixel));
        const auto to = std::min(m_peaks->buckets.size(), static_cast<std::size_t>(first + ((x + 1) * perPixel)) + 1);
        float peak = 0.0F;
        for (std::size_t i = from; i < to; ++i) {
            peak = std::max(peak, m_peaks->buckets[i]);
        }
        return peak;
    };
    QPainterPath shape;
    shape.moveTo(0, mid);
    for (int x = 0; x < width(); ++x) {
        shape.lineTo(x, mid - (peakAt(x) * half));
    }
    for (int x = width() - 1; x >= 0; --x) {
        shape.lineTo(x, mid + (peakAt(x) * half));
    }
    shape.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_theme->color(theme::Token::WaveformFillColor));
    painter.drawPath(shape);
}

/// Bar lines solid, beat lines dotted, bar numbers at the top (components.md section 2.5).
void AudioTrackView::paintGrid(QPainter &painter) const
{
    if (!m_map || !m_score) {
        return;
    }
    const QColor bar = m_theme->color(theme::Token::WaveformGridBarColor);
    QPen beatPen(m_theme->color(theme::Token::WaveformGridBeatColor), 1);
    beatPen.setStyle(Qt::DotLine);
    painter.setFont(m_theme->font(theme::TypeStyle::Caption, true));
    for (std::size_t m = 0; m < m_score->measures.size(); ++m) {
        const model::Tick start = m_score->measureStartTick(m);
        const model::TimeSignature meter = m_score->timeSigAt(m);
        const model::Tick beatTicks = model::baseTicks(model::beatDur(meter));
        for (int beat = 1; beat < meter.count; ++beat) {
            const int x = xAt(m_map->tickToSeconds(start + (beat * beatTicks)));
            painter.setPen(beatPen);
            painter.drawLine(x, 0, x, trackHeight());
        }
        const int x = xAt(m_map->tickToSeconds(start));
        painter.setPen(QPen(bar, 1));
        painter.drawLine(x, 0, x, trackHeight());
        painter.drawText(QPoint(x + barNumberInset, painter.fontMetrics().ascent() + barNumberInset),
                         QString::number(m + 1));
    }
}

void AudioTrackView::paintSyncPoints(QPainter &painter) const
{
    if (!m_map) {
        return;
    }
    const QColor color = m_theme->color(theme::Token::SyncPointColor);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.setFont(m_theme->font(theme::TypeStyle::Caption, true));
    const int bottom = trackHeight();
    for (const model::SyncPoint &point : m_points) {
        const int x = xAt(point.seconds);
        QPainterPath triangle;
        triangle.moveTo(x, bottom - syncTriangleHeight);
        triangle.lineTo(x - syncTriangleHalf, bottom);
        triangle.lineTo(x + syncTriangleHalf, bottom);
        triangle.closeSubpath();
        painter.drawPath(triangle);
        const double bpm = m_map->segmentAtSample(m_map->tickToSample(point.tick)).bpm;
        painter.setPen(color);
        painter.drawText(QPoint(x + syncTriangleHalf + barNumberInset, bottom - barNumberInset),
                         QString::number(std::lround(bpm)));
        painter.setPen(Qt::NoPen);
    }
}

void AudioTrackView::paintSelectionAndPlayhead(QPainter &painter) const
{
    if (m_selection && m_map && m_score) {
        const auto [from, to] = *m_selection;
        const int x0 = xAt(m_map->tickToSeconds(m_score->measureStartTick(from)));
        const int x1 = xAt(m_map->tickToSeconds(m_score->measureStartTick(std::min(to + 1, m_score->measures.size()))));
        QColor fill = m_theme->color(theme::Token::SelectionColor);
        fill.setAlphaF(selectionAlpha);
        painter.fillRect(QRect(x0, 0, std::max(1, x1 - x0), trackHeight()), fill);
    }
    if (m_playhead) {
        const int x = xAt(*m_playhead);
        painter.setPen(QPen(m_theme->color(theme::Token::PlayheadColor), playheadWidthPx));
        painter.drawLine(x, 0, x, trackHeight());
    }
}

// --- mouse --------------------------------------------------------------------------------------------------

void AudioTrackView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    m_dragAnchor = measureAt(event->pos().x());
    if (!m_dragAnchor) {
        emit seekRequested(secondsAt(event->pos().x()));
    }
}

void AudioTrackView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragAnchor) {
        return;
    }
    if (const auto here = measureAt(event->pos().x())) {
        setSelectedMeasures(std::pair{std::min(*m_dragAnchor, *here), std::max(*m_dragAnchor, *here)});
    }
}

/// A click without a drag seeks; a drag ends as a measure selection.
void AudioTrackView::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragAnchor) {
        return;
    }
    const auto here = measureAt(event->pos().x());
    if (here && *here != *m_dragAnchor) {
        const std::pair<std::size_t, std::size_t> range{std::min(*m_dragAnchor, *here), std::max(*m_dragAnchor, *here)};
        setSelectedMeasures(range);
        emit measuresSelected(range.first, range.second);
    } else {
        setSelectedMeasures(std::nullopt);
        emit seekRequested(secondsAt(event->pos().x()));
    }
    m_dragAnchor.reset();
}

void AudioTrackView::mouseDoubleClickEvent(QMouseEvent *event)
{
    m_dragAnchor.reset();
    emit syncPointRequested(secondsAt(event->pos().x()));
}

} // namespace cc::ui
