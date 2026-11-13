// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/media/peaks.h"
#include "core/model/score.h"
#include "core/model/timemap.h"

#include <QWidget>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QScrollBar;

namespace cc::ui
{

class Theme;

/// The audio track (components.md section 2.5, M5): the recording's waveform, the bar grid where the tick
/// map puts it, bar numbers, sync points as triangles with their tempo, the playhead and the measure
/// selection. Drag selects measures, a double click asks for a sync point at that time, a click seeks.
/// Zoom 1 fits the whole file; zoomed in, a scroll bar appears below and the view follows the playhead.
class AudioTrackView final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int preferredHeight = 120; // copy-spec.md section 16.2 C-4
    static constexpr double maxZoom = 64.0;

    explicit AudioTrackView(const Theme &theme, QWidget *parent = nullptr);

    void setPeaks(std::optional<media::Peaks> peaks);
    /// The grid: the score's measure tempos pinned by the version's sync points (seconds).
    void setTiming(const model::Score &score, std::vector<model::SyncPoint> points);
    void clearTiming();
    /// Grey dashed lines where an engine thinks bars start (copy-spec.md section 4); shown until accepted or hidden.
    void setPredictedDownbeats(std::vector<double> seconds);
    void setPredictionVisible(bool visible);
    [[nodiscard]] bool predictionVisible() const;
    /// The current position; shown whether or not the engine runs, scrolled into view when zoomed.
    void setPlayheadSeconds(std::optional<double> seconds);
    void setSelectedMeasures(std::optional<std::pair<std::size_t, std::size_t>> range);
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> selectedMeasures() const;

    /// 1 = the whole file across the width, up to maxZoom; the left edge stays where it is.
    void setZoom(double factor);
    /// Multiplies the zoom, keeping the time under viewport x where it is (Ctrl + wheel, the zoom actions).
    void zoomBy(double factor, int aroundX);
    [[nodiscard]] double zoom() const;
    /// The time at the left edge (0 at zoom 1).
    void setScrollSeconds(double seconds);
    [[nodiscard]] double scrollSeconds() const;
    [[nodiscard]] double visibleSeconds() const;

    [[nodiscard]] double secondsAt(int x) const; // viewport x -> time
    [[nodiscard]] int xAt(double seconds) const;
    [[nodiscard]] std::optional<std::size_t> measureAt(int x) const;
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void seekRequested(double seconds);
    void syncPointRequested(double seconds); // double click: "a bar starts here"
    void measuresSelected(std::size_t from, std::size_t to);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void paintWaveform(QPainter &painter) const;
    void paintGrid(QPainter &painter) const;
    void paintSyncPoints(QPainter &painter) const;
    void paintPrediction(QPainter &painter) const;
    void paintSelectionAndPlayhead(QPainter &painter) const;
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] int trackHeight() const; // the strip above the scroll bar
    void layoutScrollBar();
    void syncScrollBar(); // range and value from the zoom and the scroll
    void keepPlayheadVisible();

    const Theme *m_theme;
    QScrollBar *m_scrollBar;
    std::optional<media::Peaks> m_peaks;
    std::optional<model::Score> m_score;
    std::vector<model::SyncPoint> m_points;
    std::unique_ptr<model::TimeMap> m_map; // milliseconds as the sample rate
    std::vector<double> m_predicted; // downbeats, seconds
    bool m_predictionVisible = true;
    std::optional<double> m_playhead;
    std::optional<std::pair<std::size_t, std::size_t>> m_selection;
    std::optional<std::size_t> m_dragAnchor;
    double m_zoom = 1.0;
    double m_scrollSeconds = 0.0;
};

} // namespace cc::ui
