// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/timelineview.h"

#include "ui/routine/routinesession.h"
#include "ui/theme/theme.h"

#include <QMouseEvent>
#include <QPainter>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int barWidth = 5;
constexpr int playheadWidth = 3;
constexpr std::array<const char *, 8> circled{"①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧"};

} // namespace

TimelineView::TimelineView(const Theme &theme, RoutineSession &session, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_session(&session)
{
    setFixedHeight(cellHeight + (2 * theme.spacing(theme::Spacing::Unit)));
    setAccessibleName(tr("타임라인"));
    connect(m_session, &RoutineSession::slotsChanged, this, [this] {
        update();
    });
    connect(m_session, &RoutineSession::selectionChanged, this, [this](int) {
        update();
    });
    connect(m_session, &RoutineSession::playingSlotChanged, this, [this](int) {
        update();
    });
}

void TimelineView::setProgress(double fraction)
{
    m_progress = fraction;
    update();
}

QSize TimelineView::sizeHint() const
{
    return {RoutineSession::maxSlots * 96, height()};
}

int TimelineView::slotAt(int x) const
{
    const int count = static_cast<int>(m_session->slotList().size());
    if (count == 0) {
        return -1;
    }
    const int cell = width() / count;
    return std::clamp(x / std::max(1, cell), 0, count - 1);
}

void TimelineView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const std::vector<Slot> &slotList = m_session->slotList();
    const int count = static_cast<int>(slotList.size());
    if (count == 0) {
        return;
    }
    const qreal cell = static_cast<qreal>(width()) / count;
    const int gap = m_theme->spacing(theme::Spacing::Unit);
    const qreal radius = m_theme->shape(theme::Shape::ControlRadius).value_or(0.0) / 2.0;
    for (int i = 0; i < count; ++i) {
        const Slot &slot = slotList[static_cast<std::size_t>(i)];
        const QRectF box((i * cell) + (gap / 2.0), gap, cell - gap, cellHeight);
        const bool current = i == m_session->playingSlot();
        const bool selected = i == m_session->selectedSlot();
        painter.setPen(QPen(m_theme->color(current || selected ? Token::AccentColorDefault : Token::BorderColorDefault),
                            current ? 2.0 : 1.0));
        painter.setBrush(m_theme->color(Token::BackgroundLayer3Color));
        painter.drawRoundedRect(box, radius, radius);
        painter.fillRect(QRectF(box.left() + 1, box.top() + 1, barWidth, box.height() - 2),
                         m_theme->color(slot.chordColor));
        painter.setPen(m_theme->color(Token::ContentColorDefault));
        painter.setFont(m_theme->font(theme::TypeStyle::Caption));
        const QString number =
            i < 8 ? QString::fromUtf8(circled[static_cast<std::size_t>(i)]) + QLatin1Char(' ') : QString();
        const QString text = slot.roman.isEmpty() ? slot.name : slot.roman;
        painter.drawText(box.adjusted(barWidth + (gap * 2), 0, -gap, 0),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         number + text);
        painter.setPen(m_theme->color(Token::ContentColorSecondary));
        painter.drawText(box.adjusted(0, 0, -gap * 2, 0), Qt::AlignVCenter | Qt::AlignRight, slot.rootString);
    }
    if (m_progress >= 0.0) {
        const qreal x = m_progress * width();
        painter.fillRect(QRectF(x - (playheadWidth / 2.0), 0, playheadWidth, height()),
                         m_theme->color(Token::PlayheadColor));
    }
}

void TimelineView::mousePressEvent(QMouseEvent *event)
{
    const int slot = slotAt(event->pos().x());
    if (slot >= 0) {
        m_session->setSelectedSlot(slot);
    }
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int slot = slotAt(event->pos().x());
    if (slot >= 0) {
        emit seekRequested(slot);
    }
}

} // namespace cc::ui
