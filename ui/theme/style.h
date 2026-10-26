// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QProxyStyle>

// Fusion with the derived colours replaced by tokens (docs/ui/qpalette.md section 4, components.md section 0.1):
// flat 1 px frames, hover / pressed overlays, accent-filled "on" tool buttons, a 2 px outer focus ring, and the
// chrome separators (toolbar bottom, status bar top, dock edges). One instance for the whole run; it reads the
// Theme live, so a theme switch needs no new style.
namespace cc::ui
{

class Theme;

class CopyCoachStyle final : public QProxyStyle
{
    Q_OBJECT
public:
    explicit CopyCoachStyle(const Theme &theme);

    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption *option,
                       QPainter *painter,
                       const QWidget *widget) const override;
    void drawControl(ControlElement element,
                     const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget) const override;
    [[nodiscard]] int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override;
    [[nodiscard]] QPalette standardPalette() const override;

private:
    void drawControlPanel(const QStyleOption *option, QPainter *painter, bool raised) const;
    void drawToolButtonPanel(const QStyleOption *option, QPainter *painter) const;
    void drawFocusRing(const QStyleOption *option, QPainter *painter) const;
    void drawEdge(const QStyleOption *option, QPainter *painter, Qt::Edge edge) const;
    [[nodiscard]] qreal controlRadius() const;

    const Theme *m_theme;
};

} // namespace cc::ui
