// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/theme/style.h"

#include "ui/theme/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>

namespace cc::ui
{

namespace
{

using theme::Shape;
using theme::Token;

constexpr qreal halfPixel = 0.5;
constexpr int focusRingWidth = 2;
constexpr int chromeLineWidth = 1;

QRectF innerRect(const QRect &rect)
{
    return QRectF(rect).adjusted(halfPixel, halfPixel, -halfPixel, -halfPixel);
}

void fillRounded(QPainter *painter, const QRectF &rect, qreal radius, const QColor &fill, const QColor &outline)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    painter->fillPath(path, fill);
    if (outline.isValid()) {
        painter->setPen(QPen(outline, 1.0));
        painter->drawPath(path);
    }
    painter->restore();
}

} // namespace

CopyCoachStyle::CopyCoachStyle(const Theme &theme)
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
    , m_theme(&theme)
{
}

qreal CopyCoachStyle::controlRadius() const
{
    // Shape token is a Swing arc (diameter); the corner radius is half of it (components.md section 0.1).
    return m_theme->shape(Shape::ControlRadius).value_or(0.0) / 2.0;
}

void CopyCoachStyle::drawControlPanel(const QStyleOption *option, QPainter *painter, bool raised) const
{
    const bool enabled = (option->state & State_Enabled) != 0;
    const Token fillToken = raised ? Token::BackgroundLayer3Color : Token::BackgroundInlineColor;
    const Token border = enabled ? Token::BorderColorEmphasis : Token::BorderColorDefault;
    fillRounded(painter, innerRect(option->rect), controlRadius(), m_theme->color(fillToken), m_theme->color(border));
    if (raised && enabled && (option->state & State_Sunken) != 0) {
        fillRounded(painter,
                    innerRect(option->rect),
                    controlRadius(),
                    m_theme->color(Token::BackgroundPressedColor),
                    QColor());
    } else if (raised && enabled && (option->state & State_MouseOver) != 0) {
        fillRounded(painter,
                    innerRect(option->rect),
                    controlRadius(),
                    m_theme->color(Token::BackgroundHoverColor),
                    QColor());
    }
}

void CopyCoachStyle::drawToolButtonPanel(const QStyleOption *option, QPainter *painter) const
{
    const bool on = (option->state & State_On) != 0;
    const bool enabled = (option->state & State_Enabled) != 0;
    if (on) {
        fillRounded(painter,
                    innerRect(option->rect),
                    controlRadius(),
                    m_theme->color(Token::AccentColorDefault),
                    QColor());
        return;
    }
    if (!enabled) {
        return;
    }
    if ((option->state & State_Sunken) != 0) {
        fillRounded(painter,
                    innerRect(option->rect),
                    controlRadius(),
                    m_theme->color(Token::BackgroundPressedColor),
                    QColor());
    } else if ((option->state & State_MouseOver) != 0) {
        fillRounded(painter,
                    innerRect(option->rect),
                    controlRadius(),
                    m_theme->color(Token::BackgroundHoverColor),
                    QColor());
    }
}

void CopyCoachStyle::drawFocusRing(const QStyleOption *option, QPainter *painter) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(m_theme->color(Token::FocusColor), focusRingWidth));
    painter->setBrush(Qt::NoBrush);
    const QRectF ring = QRectF(option->rect).adjusted(1.0, 1.0, -1.0, -1.0);
    painter->drawRoundedRect(ring, controlRadius(), controlRadius());
    painter->restore();
}

void CopyCoachStyle::drawEdge(const QStyleOption *option, QPainter *painter, Qt::Edge edge) const
{
    painter->fillRect(option->rect, m_theme->color(Token::BackgroundCanvasColor));
    const QRect r = option->rect;
    const QColor line = m_theme->color(Token::BorderColorDefault);
    if (edge == Qt::BottomEdge) {
        painter->fillRect(QRect(r.left(), r.bottom() - chromeLineWidth + 1, r.width(), chromeLineWidth), line);
    } else {
        painter->fillRect(QRect(r.left(), r.top(), r.width(), chromeLineWidth), line);
    }
}

void CopyCoachStyle::drawPrimitive(PrimitiveElement element,
                                   const QStyleOption *option,
                                   QPainter *painter,
                                   const QWidget *widget) const
{
    switch (element) {
    case PE_PanelButtonCommand:
        drawControlPanel(option, painter, true);
        return;
    case PE_Frame:
    case PE_FrameLineEdit:
        drawControlPanel(option, painter, false);
        return;
    case PE_PanelButtonTool:
        drawToolButtonPanel(option, painter);
        return;
    case PE_FrameFocusRect:
        drawFocusRing(option, painter);
        return;
    case PE_PanelStatusBar:
        drawEdge(option, painter, Qt::TopEdge);
        return;
    case PE_IndicatorDockWidgetResizeHandle:
        painter->fillRect(option->rect, m_theme->color(Token::BorderColorDefault));
        return;
    case PE_FrameDockWidget:
        painter->setPen(m_theme->color(Token::BorderColorDefault));
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        return;
    case PE_IndicatorToolBarSeparator:
        painter->fillRect(option->rect.adjusted(option->rect.width() / 2, 2, -option->rect.width() / 2, -2),
                          m_theme->color(Token::SeparatorColor));
        return;
    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
}

void CopyCoachStyle::drawControl(ControlElement element,
                                 const QStyleOption *option,
                                 QPainter *painter,
                                 const QWidget *widget) const
{
    if (element == CE_ToolBar) {
        drawEdge(option, painter, Qt::BottomEdge);
        return;
    }
    if (element == CE_MenuBarEmptyArea) {
        painter->fillRect(option->rect, m_theme->color(Token::BackgroundCanvasColor));
        return;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

int CopyCoachStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_DockWidgetSeparatorExtent:
        return chromeLineWidth;
    case PM_ToolBarItemMargin:
    case PM_ToolBarFrameWidth:
        return 0;
    case PM_ToolBarItemSpacing:
        return m_theme->spacing(theme::Spacing::Unit);
    case PM_FocusFrameHMargin:
    case PM_FocusFrameVMargin:
        return focusRingWidth;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QPalette CopyCoachStyle::standardPalette() const
{
    return m_theme->palette(Surface::Canvas);
}

} // namespace cc::ui
