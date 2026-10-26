// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QWidget>

// Timeline (components.md section 2.3): one cell per slot with its colour bar and roman numeral, the current
// cell outlined in accent, and a playhead at the transport position. Click = select, double-click = seek.
namespace cc::ui
{

class RoutineSession;
class Theme;

class TimelineView final : public QWidget
{
    Q_OBJECT
public:
    static constexpr int cellHeight = 44;

    TimelineView(const Theme &theme, RoutineSession &session, QWidget *parent = nullptr);

    /// Playhead as a fraction 0..1 of the whole loop; negative hides it.
    void setProgress(double fraction);
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void seekRequested(int slot);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] int slotAt(int x) const;

    const Theme *m_theme;
    RoutineSession *m_session;
    double m_progress = -1.0;
};

} // namespace cc::ui
