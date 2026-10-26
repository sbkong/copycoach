// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/fretboard/fretboardview.h"

#include "ui/theme/theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

#include <array>
#include <cmath>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int fretCount = FretboardView::lastFret - FretboardView::firstFret + 1;
constexpr std::array<int, 9> markerFrets{3, 5, 7, 9, 12, 15, 17, 19, 21};
constexpr int doubleMarkerFret = 12;
constexpr qreal markerRadius = 4.0;
constexpr qreal nutWidth = 3.0;
constexpr qreal stringWidthStep = 0.3;
constexpr int normalDotPx = 24; // ia.md section 2: dot sizes 24 / 44, labels 12 / 18
constexpr int largeDotPx = 44;
constexpr int normalLabelPx = 12;
constexpr int largeLabelPx = 18;
constexpr qreal lightnessForDarkInk = 0.6;

/// Label ink for a dot: dark text on light fills, light text on dark ones (token fretboard-dot-label-color is
/// declared `on_color(dot fill)` in the theme, i.e. computed here).
QColor inkFor(const QColor &fill, const Theme &theme)
{
    return fill.lightnessF() > lightnessForDarkInk ? theme.color(Token::BackgroundContentColor)
                                                   : theme.color(Token::AccentContentColorOnAccent);
}

} // namespace

qreal FretboardView::Geometry::fretX(int fret) const
{
    return left + ((static_cast<qreal>(fret - FretboardView::firstFret) + 0.5) * fretWidth);
}

qreal FretboardView::Geometry::stringY(int string) const
{
    return top + (static_cast<qreal>(string - 1) * stringGap);
}

FretboardView::FretboardView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
{
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("지판"));
    setMinimumHeight(minimumSizeHint().height());
}

void FretboardView::setScene(FretboardScene scene)
{
    m_scene = std::move(scene);
    update();
}

void FretboardView::setDotSize(DotSize size)
{
    m_dotSize = size;
    update();
}

void FretboardView::setStringCount(int count)
{
    m_strings = std::clamp(count, theory::minStrings, theory::maxStrings);
    update();
}

int FretboardView::dotRadius() const
{
    return (m_dotSize == DotSize::Large ? largeDotPx : normalDotPx) / 2;
}

int FretboardView::labelPx() const
{
    return m_dotSize == DotSize::Large ? largeLabelPx : normalLabelPx;
}

QSize FretboardView::sizeHint() const
{
    return {fretCount * 40, (6 * dotRadius() * 2) + (2 * m_theme->spacing(theme::Spacing::SurfaceEdge)) + 40};
}

QSize FretboardView::minimumSizeHint() const
{
    const int gap = m_theme->spacing(theme::Spacing::TargetMin); // one click target per string (WCAG 2.5.8)
    return {fretCount * 16, (m_strings * gap) + (2 * m_theme->spacing(theme::Spacing::SurfaceEdge)) + 32};
}

FretboardView::Geometry FretboardView::geometry() const
{
    const qreal edge = m_theme->spacing(theme::Spacing::SurfaceEdge);
    const qreal left = edge + 24.0; // room for string names
    const qreal top = edge + 28.0; // room for the caption and fret numbers
    const qreal fretWidth = (width() - left - edge) / fretCount;
    const qreal stringGap = (height() - top - edge) / std::max(1, m_strings - 1);
    return Geometry{left, top, fretWidth, stringGap};
}

void FretboardView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), m_theme->color(Token::BackgroundContentColor));
    const Geometry g = geometry();
    paintBoard(painter, g);
    paintDots(painter, g);
    if (hasFocus()) {
        QStyleOptionFocusRect option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
    }
}

void FretboardView::paintBoard(QPainter &painter, const Geometry &g) const
{
    const qreal boardTop = g.stringY(1);
    const qreal boardBottom = g.stringY(m_strings);
    painter.setFont(m_theme->font(theme::TypeStyle::Caption, true));
    for (int fret = firstFret; fret <= lastFret + 1; ++fret) {
        const qreal x = g.left + (static_cast<qreal>(fret - firstFret) * g.fretWidth);
        const bool nut = fret == 1; // the line between the open column and fret 1
        painter.setPen(QPen(m_theme->color(Token::FretboardFretColor), nut ? nutWidth : 1.0));
        painter.drawLine(QPointF(x, boardTop), QPointF(x, boardBottom));
        if (fret <= lastFret) {
            painter.setPen(m_theme->color(Token::ContentColorSecondary));
            painter.drawText(QRectF(x, g.top - 22.0, g.fretWidth, 16.0), Qt::AlignCenter, QString::number(fret));
        }
    }
    for (int string = 1; string <= m_strings; ++string) {
        const qreal y = g.stringY(string);
        painter.setPen(QPen(m_theme->color(Token::FretboardStringColor), 1.0 + ((string - 1) * stringWidthStep)));
        painter.drawLine(QPointF(g.left, y), QPointF(g.left + (fretCount * g.fretWidth), y));
    }
    painter.setBrush(m_theme->color(Token::FretboardMarkerColor));
    painter.setPen(Qt::NoPen);
    const qreal middle = (boardTop + boardBottom) / 2.0;
    for (const int fret : markerFrets) {
        const qreal x = g.fretX(fret);
        if (fret == doubleMarkerFret) {
            painter.drawEllipse(QPointF(x, (g.stringY(1) + g.stringY(2)) / 2.0), markerRadius, markerRadius);
            painter.drawEllipse(QPointF(x, (g.stringY(m_strings - 1) + g.stringY(m_strings)) / 2.0),
                                markerRadius,
                                markerRadius);
        } else {
            painter.drawEllipse(QPointF(x, middle), markerRadius, markerRadius);
        }
    }
    if (!m_scene.caption.isEmpty()) {
        painter.setFont(m_theme->font(theme::TypeStyle::BodyStrong));
        painter.setPen(m_theme->color(Token::AccentColorDefault));
        painter.drawText(QPointF(g.left, g.top - 28.0), m_scene.caption);
    }
}

void FretboardView::paintDotShape(QPainter &painter, const FretDot &dot, const QRectF &box) const
{
    const qreal r = box.width() / 2.0;
    if (dot.shape == DotShape::Dashed) {
        painter.setBrush(m_theme->color(Token::BackgroundLayer3Color));
        painter.setPen(QPen(m_theme->color(Token::BorderColorEmphasis), 1.5, Qt::DashLine));
        painter.drawEllipse(box);
        painter.setPen(m_theme->color(Token::ContentColorSecondary));
        return;
    }
    const QColor outline =
        dot.emphasized ? m_theme->color(Token::AccentColorDefault) : m_theme->color(Token::BackgroundContentColor);
    painter.setBrush(dot.fill);
    painter.setPen(QPen(outline, dot.emphasized ? 3.0 : 2.0));
    if (dot.shape == DotShape::Diamond) {
        painter.save();
        painter.translate(box.center());
        painter.rotate(45.0);
        painter.drawRoundedRect(QRectF(-r * 0.8, -r * 0.8, r * 1.6, r * 1.6), 2.0, 2.0);
        painter.restore();
    } else {
        painter.drawEllipse(box);
    }
    painter.setPen(inkFor(dot.fill, *m_theme));
}

void FretboardView::paintDots(QPainter &painter, const Geometry &g) const
{
    const qreal r = dotRadius();
    QFont labelFont = m_theme->font(theme::TypeStyle::Caption);
    labelFont.setPixelSize(labelPx());
    labelFont.setWeight(QFont::Bold);
    painter.setFont(labelFont);
    for (const FretDot &dot : m_scene.dots) {
        const QPointF center(g.fretX(dot.position.fret), g.stringY(dot.position.string));
        const QRectF box(center.x() - r, center.y() - r, 2 * r, 2 * r);
        paintDotShape(painter, dot, box);
        painter.drawText(box, Qt::AlignCenter, dot.label);
    }
}

std::optional<theory::StringFret> FretboardView::hitTest(QPointF point) const
{
    const Geometry g = geometry();
    const int fret = static_cast<int>(std::floor((point.x() - g.left) / g.fretWidth)) + firstFret;
    const int string = static_cast<int>(std::lround((point.y() - g.top) / g.stringGap)) + 1;
    if (fret < firstFret || fret > lastFret || string < 1 || string > m_strings) {
        return std::nullopt;
    }
    return theory::StringFret{string, fret};
}

void FretboardView::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (const auto hit = hitTest(event->position())) {
        emit positionClicked(*hit);
    }
}

} // namespace cc::ui
