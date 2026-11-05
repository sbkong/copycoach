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

namespace cc::ui
{

class Theme;

/// The audio track (components.md section 2.5, M5): the recording's waveform across the width, the bar grid
/// where the tick map puts it, bar numbers, sync points as triangles with their tempo, the playhead and the
/// measure selection. Drag selects measures, a double click asks for a sync point at that time, a click seeks.
/// The whole file fits the width (zoom comes later).
class AudioTrackView final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int preferredHeight = 120; // copy-spec.md section 16.2 C-4

    explicit AudioTrackView(const Theme &theme, QWidget *parent = nullptr);

    void setPeaks(std::optional<media::Peaks> peaks);
    /// The grid: the score's measure tempos pinned by the version's sync points (seconds).
    void setTiming(const model::Score &score, std::vector<model::SyncPoint> points);
    void clearTiming();
    void setPlayheadSeconds(std::optional<double> seconds);
    void setSelectedMeasures(std::optional<std::pair<std::size_t, std::size_t>> range);
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> selectedMeasures() const;

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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void paintWaveform(QPainter &painter) const;
    void paintGrid(QPainter &painter) const;
    void paintSyncPoints(QPainter &painter) const;
    void paintSelectionAndPlayhead(QPainter &painter) const;
    [[nodiscard]] double durationSeconds() const;

    const Theme *m_theme;
    std::optional<media::Peaks> m_peaks;
    std::optional<model::Score> m_score;
    std::vector<model::SyncPoint> m_points;
    std::unique_ptr<model::TimeMap> m_map; // milliseconds as the sample rate
    std::optional<double> m_playhead;
    std::optional<std::pair<std::size_t, std::size_t>> m_selection;
    std::optional<std::size_t> m_dragAnchor;
};

} // namespace cc::ui
