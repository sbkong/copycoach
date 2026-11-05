// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/scoreview.h"

#include "ui/theme/theme.h"

#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

namespace cc::ui
{

ScoreView::ScoreView(const Theme &theme, const std::filesystem::path &verovioResources, QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_theme(&theme)
    , m_document(verovioResources)
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAccessibleName(tr("악보"));
    applyTheme();
    connect(m_theme, &Theme::changed, this, &ScoreView::applyTheme);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}

void ScoreView::applyTheme()
{
    m_gap = m_theme->spacing(theme::Spacing::ContentArea);
    m_document.setInk(m_theme->color(theme::Token::ScoreNotationColor),
                      m_theme->fontFamilies(theme::FontKey::FontFamilyScoreText).value(0));
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Window, m_theme->color(theme::Token::BackgroundCanvasColor));
    pal.setColor(QPalette::Base, m_theme->color(theme::Token::BackgroundCanvasColor));
    viewport()->setPalette(pal);
    viewport()->setBackgroundRole(QPalette::Base);
    viewport()->setAutoFillBackground(true);
    viewport()->update();
}

void ScoreView::setScore(const model::Score &score)
{
    m_document.setPageWidth(static_cast<int>(viewport()->width() / m_zoom));
    m_document.setScore(score);
    relayout();
}

void ScoreView::clear()
{
    m_document.clear();
    relayout();
}

bool ScoreView::hasScore() const
{
    return m_document.hasScore();
}

int ScoreView::pageCount() const
{
    return m_document.pageCount();
}

void ScoreView::setZoom(double factor)
{
    const double zoom = std::clamp(factor, minZoom, maxZoom);
    if (zoom == m_zoom) {
        return;
    }
    m_zoom = zoom;
    m_document.setPageWidth(static_cast<int>(viewport()->width() / m_zoom));
    relayout();
}

double ScoreView::zoom() const
{
    return m_zoom;
}

ScoreDocument &ScoreView::document()
{
    return m_document;
}

double ScoreView::pageTop(int page)
{
    double top = m_gap;
    for (int i = 0; i < page; ++i) {
        top += (m_document.page(i).sizePx.height() * m_zoom) + m_gap;
    }
    return top;
}

std::optional<QRectF> ScoreView::rectFor(const std::string &id)
{
    for (int page = 0; page < m_document.pageCount(); ++page) {
        if (const auto rect = m_document.page(page).geometry.rectFor(id)) {
            const QRectF zoomed(rect->topLeft() * m_zoom, rect->size() * m_zoom);
            return zoomed.translated(-horizontalScrollBar()->value() + m_gap,
                                     pageTop(page) - verticalScrollBar()->value());
        }
    }
    return std::nullopt;
}

std::string ScoreView::elementAt(QPoint viewportPoint, const QString &className)
{
    const double docY = viewportPoint.y() + verticalScrollBar()->value();
    for (int page = 0; page < m_document.pageCount(); ++page) {
        const double top = pageTop(page);
        const double height = m_document.page(page).sizePx.height() * m_zoom;
        if (docY < top || docY >= top + height) {
            continue;
        }
        const QPointF pagePoint((viewportPoint.x() + horizontalScrollBar()->value() - m_gap) / m_zoom,
                                (docY - top) / m_zoom);
        return m_document.page(page).geometry.elementAt(pagePoint, className);
    }
    return {};
}

void ScoreView::relayout()
{
    double height = m_gap;
    double width = 0.0;
    for (int page = 0; page < m_document.pageCount(); ++page) {
        const QSizeF size = m_document.page(page).sizePx * m_zoom;
        height += size.height() + m_gap;
        width = std::max(width, size.width());
    }
    verticalScrollBar()->setRange(0, std::max(0, static_cast<int>(std::ceil(height)) - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(
        0,
        std::max(0, static_cast<int>(std::ceil(width)) + (2 * m_gap) - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    viewport()->update();
}

void ScoreView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    if (m_document.hasScore()) {
        m_document.setPageWidth(static_cast<int>(viewport()->width() / m_zoom));
    }
    relayout();
}

void ScoreView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(viewport());
    if (!m_document.hasScore()) {
        paintEmptyState(painter);
        return;
    }
    paintPages(painter);
}

/// states.md section 3: the copy centre with no project.
void ScoreView::paintEmptyState(QPainter &painter)
{
    painter.setPen(m_theme->color(theme::Token::ContentColorSecondary));
    painter.setFont(m_theme->font(theme::TypeStyle::BodyStrong));
    const QRect area = viewport()->rect();
    painter.drawText(area, Qt::AlignCenter, tr("아직 곡이 없습니다\n파일 › 불러오기로 MusicXML 악보를 여세요"));
}

void ScoreView::paintPages(QPainter &painter)
{
    const int scrollY = verticalScrollBar()->value();
    const int scrollX = horizontalScrollBar()->value();
    for (int page = 0; page < m_document.pageCount(); ++page) {
        const double top = pageTop(page) - scrollY;
        const ScoreDocument::Page &p = m_document.page(page);
        const double height = p.sizePx.height() * m_zoom;
        if (top + height < 0 || top > viewport()->height()) {
            continue;
        }
        painter.save();
        painter.translate(m_gap - scrollX, top);
        painter.scale(m_zoom, m_zoom);
        painter.drawPicture(0, 0, p.picture);
        painter.restore();
    }
}

} // namespace cc::ui
