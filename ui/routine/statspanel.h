// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QWidget>

class QLabel;

// Stats dock, routine context (stats.md section 3, components.md section 2 "기록 도크"): today's minutes per
// module as bars against the timer targets, the streak, and the key x module heatmap of the last 28 days.
// Both charts are custom painters that read colours only through Theme (stats.md section 5). Empty state
// text from stats.md section 7.
namespace cc::ui
{

class RoutineTimerController;
class StatsModel;
class Theme;

/// One row per module: label, a bar of today's seconds against practice.timer-minutes, "N / M분".
class ModuleBarsView final : public QWidget
{
    Q_OBJECT
public:
    ModuleBarsView(const Theme &theme,
                   const StatsModel &model,
                   const RoutineTimerController &timer,
                   QWidget *parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    [[nodiscard]] int rowHeight() const;
    void paintRow(QPainter &painter, int module, const QRect &row, int labelWidth, int valueWidth) const;

    const Theme *m_theme;
    const StatsModel *m_model;
    const RoutineTimerController *m_timer;
};

/// 12 keys x 4 modules; a cell's accent alpha grows in four steps with its share of the busiest cell.
class CoverageHeatmapView final : public QWidget
{
    Q_OBJECT
public:
    CoverageHeatmapView(const Theme &theme, const StatsModel &model, QWidget *parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    /// Alpha step 0..4 of a cell (0 = empty); the test reads it instead of pixels.
    [[nodiscard]] int level(int module, int pitchClass) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    [[nodiscard]] int count(int module, int pitchClass) const;
    [[nodiscard]] int maxCount() const;
    [[nodiscard]] int labelWidth() const;
    [[nodiscard]] int headerHeight() const;
    [[nodiscard]] int cellSize() const;
    void paintHeader(QPainter &painter) const;
    void paintCells(QPainter &painter) const;

    const Theme *m_theme;
    const StatsModel *m_model;
};

class StatsPanel final : public QWidget
{
    Q_OBJECT
public:
    StatsPanel(const Theme &theme,
               const StatsModel &model,
               const RoutineTimerController &timer,
               QWidget *parent = nullptr);

    [[nodiscard]] bool isEmptyStateShown() const;
    [[nodiscard]] CoverageHeatmapView *heatmap() const;

private:
    void sync();
    void applyTheme();

    const Theme *m_theme;
    const StatsModel *m_model;
    QLabel *m_empty;
    QWidget *m_content;
    QLabel *m_streak;
    CoverageHeatmapView *m_heatmap;
};

} // namespace cc::ui
