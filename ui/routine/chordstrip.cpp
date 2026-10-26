// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/chordstrip.h"

#include "generated/actionids.h"
#include "ui/routine/routinesession.h"
#include "ui/shell/actionregistry.h"
#include "ui/theme/theme.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

#include <utility>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int barWidth = 5;
constexpr int toggleWidth = 18;
constexpr int chipHeight = 40;
constexpr std::array<const char *, 8> circled{"①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧"};

QToolButton *toolButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace

ChordChip::ChordChip(const Theme &theme, int slot, QWidget *parent)
    : QAbstractButton(parent)
    , m_theme(&theme)
    , m_slot(slot)
{
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
}

void ChordChip::setSlotState(const QString &title, const QString &detail, const QColor &bar, bool muted, bool solo)
{
    m_title = title;
    m_detail = detail;
    m_bar = bar;
    m_muted = muted;
    m_solo = solo;
    setAccessibleName(title + QStringLiteral(" ") + detail);
    update();
}

void ChordChip::setPlaybackState(bool current, bool selected, bool next)
{
    m_current = current;
    m_selected = selected;
    m_next = next;
    update();
}

QSize ChordChip::sizeHint() const
{
    const QFontMetrics body(m_theme->font(theme::TypeStyle::BodyStrong));
    const QFontMetrics caption(m_theme->font(theme::TypeStyle::Caption));
    const int text = std::max(body.horizontalAdvance(m_title), caption.horizontalAdvance(m_detail));
    return {barWidth + toggleWidth + text + (3 * m_theme->spacing(theme::Spacing::Control)), chipHeight};
}

QRect ChordChip::muteRect() const
{
    const int gap = m_theme->spacing(theme::Spacing::Unit);
    return {barWidth + gap, gap, toggleWidth, (height() / 2) - gap};
}

QRect ChordChip::soloRect() const
{
    const int gap = m_theme->spacing(theme::Spacing::Unit);
    return {barWidth + gap, height() / 2, toggleWidth, (height() / 2) - gap};
}

void ChordChip::paintToggle(QPainter &painter, const QRect &rect, const QString &text, bool on) const
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? m_theme->color(Token::AccentColorDefault) : m_theme->color(Token::BackgroundLayer2Color));
    painter.drawRoundedRect(rect, 3.0, 3.0);
    painter.setPen(on ? m_theme->color(Token::AccentContentColorOnAccent)
                      : m_theme->color(Token::ContentColorSecondary));
    painter.setFont(m_theme->font(theme::TypeStyle::Caption));
    painter.drawText(rect, Qt::AlignCenter, text);
}

void ChordChip::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal radius = m_theme->shape(theme::Shape::ControlRadius).value_or(0.0) / 2.0;
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPen outline(m_theme->color(Token::BorderColorDefault), 1.0);
    if (m_current) {
        outline = QPen(m_theme->color(Token::AccentColorDefault), 2.0);
    } else if (m_selected) {
        outline = QPen(m_theme->color(Token::AccentColorDefault), 1.0);
    } else if (m_next) {
        outline = QPen(m_theme->color(Token::AccentColorDefault), 1.0, Qt::DotLine);
    }
    painter.setPen(outline);
    painter.setBrush(m_selected ? m_theme->color(Token::AccentBackgroundColorDefault)
                                : m_theme->color(Token::BackgroundLayer3Color));
    painter.drawRoundedRect(box, radius, radius);
    painter.fillRect(QRect(1, 1, barWidth, height() - 2), m_bar);
    paintToggle(painter, muteRect(), QStringLiteral("M"), m_muted);
    paintToggle(painter, soloRect(), QStringLiteral("S"), m_solo);
    const int textX = barWidth + toggleWidth + (2 * m_theme->spacing(theme::Spacing::Control));
    const Token ink = m_muted ? Token::ContentColorDisabled : Token::ContentColorDefault;
    painter.setPen(m_theme->color(ink));
    painter.setFont(m_theme->font(theme::TypeStyle::BodyStrong));
    painter.drawText(QRect(textX, 0, width() - textX, (height() / 2) + 2), Qt::AlignLeft | Qt::AlignBottom, m_title);
    painter.setPen(m_theme->color(m_muted ? Token::ContentColorDisabled : Token::ContentColorSecondary));
    painter.setFont(m_theme->font(theme::TypeStyle::Caption));
    painter.drawText(QRect(textX, height() / 2, width() - textX, (height() / 2) - 2),
                     Qt::AlignLeft | Qt::AlignTop,
                     m_detail);
}

void ChordChip::mousePressEvent(QMouseEvent *event)
{
    if (muteRect().contains(event->pos())) {
        emit muteToggled(m_slot);
        return;
    }
    if (soloRect().contains(event->pos())) {
        emit soloToggled(m_slot);
        return;
    }
    QAbstractButton::mousePressEvent(event);
}

ChordStrip::ChordStrip(const Theme &theme, ActionRegistry &actions, RoutineSession &session, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_session(&session)
    , m_arp(new QToolButton(this))
    , m_chord(new QToolButton(this))
    , m_chipRow(new QHBoxLayout())
{
    auto *layout = new QHBoxLayout(this);
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    layout->setContentsMargins(edge, theme.spacing(theme::Spacing::Unit), edge, theme.spacing(theme::Spacing::Unit));
    layout->setSpacing(theme.spacing(theme::Spacing::Control));
    for (auto *button : {m_arp, m_chord}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        layout->addWidget(button);
    }
    m_arp->setText(tr("Arp"));
    m_chord->setText(tr("Chord"));
    m_arp->setAccessibleName(tr("재생 방식 Arp"));
    m_chord->setAccessibleName(tr("재생 방식 Chord"));
    layout->addWidget(toolButton(actions.action(action::Id::RoutineChordMuteAll), this));
    layout->addWidget(toolButton(actions.action(action::Id::RoutineChordClearSolo), this));
    m_chipRow->setSpacing(theme.spacing(theme::Spacing::Control));
    layout->addLayout(m_chipRow, 1);

    connect(m_arp, &QToolButton::clicked, this, [this] {
        m_session->setPlaybackStyle(routine::PlaybackStyle::Arp);
    });
    connect(m_chord, &QToolButton::clicked, this, [this] {
        m_session->setPlaybackStyle(routine::PlaybackStyle::Chord);
    });
    connect(m_session, &RoutineSession::playbackStyleChanged, this, [this](routine::PlaybackStyle) {
        refreshStates();
    });
    connect(m_session, &RoutineSession::slotsChanged, this, &ChordStrip::rebuildChips);
    connect(m_session, &RoutineSession::selectionChanged, this, [this](int) {
        refreshStates();
    });
    connect(m_session, &RoutineSession::playingSlotChanged, this, [this](int) {
        refreshStates();
    });
    connect(m_session, &RoutineSession::mixChanged, this, &ChordStrip::refreshStates);
    rebuildChips();
}

void ChordStrip::rebuildChips()
{
    for (ChordChip *chip : m_chips) {
        chip->deleteLater();
    }
    m_chips.clear();
    const std::vector<Slot> &slotList = m_session->slotList();
    for (int i = 0; std::cmp_less(i, slotList.size()); ++i) {
        auto *chip = new ChordChip(*m_theme, i, this);
        connect(chip, &ChordChip::clicked, this, [this, i] {
            m_session->setSelectedSlot(i);
        });
        connect(chip, &ChordChip::muteToggled, m_session, &RoutineSession::toggleMute);
        connect(chip, &ChordChip::soloToggled, m_session, &RoutineSession::toggleSolo);
        m_chipRow->addWidget(chip);
        m_chips.push_back(chip);
    }
    m_chipRow->addStretch();
    refreshStates();
}

void ChordStrip::refreshStates()
{
    m_arp->setChecked(m_session->playbackStyle() == routine::PlaybackStyle::Arp);
    m_chord->setChecked(m_session->playbackStyle() == routine::PlaybackStyle::Chord);
    const std::vector<Slot> &slotList = m_session->slotList();
    const int count = static_cast<int>(slotList.size());
    const int shown = m_session->playingSlot() >= 0 ? m_session->playingSlot() : m_session->selectedSlot();
    for (int i = 0; i < count && std::cmp_less(i, m_chips.size()); ++i) {
        const Slot &slot = slotList[static_cast<std::size_t>(i)];
        const QString number =
            i < 8 ? QString::fromUtf8(circled[static_cast<std::size_t>(i)]) + QLatin1Char(' ') : QString();
        const QString detail =
            slot.rootString.isEmpty() ? slot.roman : slot.roman + QStringLiteral(" · ") + slot.rootString;
        m_chips[static_cast<std::size_t>(i)]->setSlotState(number + slot.name,
                                                           detail,
                                                           m_theme->color(slot.chordColor),
                                                           m_session->isMuted(i),
                                                           m_session->isSoloed(i));
        const bool next = count > 1 && i == (shown + 1) % count;
        m_chips[static_cast<std::size_t>(i)]->setPlaybackState(i == m_session->playingSlot(),
                                                               i == m_session->selectedSlot(),
                                                               next);
    }
}

} // namespace cc::ui
