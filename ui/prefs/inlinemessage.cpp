// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/inlinemessage.h"

#include "ui/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

namespace cc::ui
{

namespace
{

using theme::Token;

constexpr int barWidth = 3;

Token barToken(MessageKind kind)
{
    switch (kind) {
    case MessageKind::Notice:
        return Token::NoticeColorDefault;
    case MessageKind::Negative:
        return Token::NegativeColorDefault;
    case MessageKind::Positive:
        return Token::PositiveColorDefault;
    case MessageKind::Info:
        break;
    }
    return Token::AccentColorDefault;
}

Token backgroundToken(MessageKind kind)
{
    switch (kind) {
    case MessageKind::Notice:
        return Token::NoticeBackgroundColorDefault;
    case MessageKind::Negative:
        return Token::NegativeBackgroundColorDefault;
    case MessageKind::Positive:
        return Token::PositiveBackgroundColorDefault;
    case MessageKind::Info:
        break;
    }
    return Token::AccentBackgroundColorDefault;
}

} // namespace

InlineMessage::InlineMessage(const Theme &theme, QWidget *parent)
    : QFrame(parent)
    , m_theme(&theme)
    , m_text(new QLabel(this))
    , m_buttons(new QHBoxLayout())
{
    auto *layout = new QHBoxLayout(this);
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    layout->setContentsMargins(edge + barWidth,
                               theme.spacing(theme::Spacing::Control),
                               edge,
                               theme.spacing(theme::Spacing::Control));
    layout->setSpacing(theme.spacing(theme::Spacing::Control));
    m_text->setWordWrap(true);
    layout->addWidget(m_text, 1);
    layout->addLayout(m_buttons);
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("×"));
    close->setToolTip(tr("닫기"));
    close->setAccessibleName(tr("메시지 닫기"));
    connect(close, &QToolButton::clicked, this, &InlineMessage::clear);
    layout->addWidget(close);
    QWidget::hide();
}

void InlineMessage::show(MessageKind kind, const QString &text, const std::vector<MessageAction> &actions)
{
    m_kind = kind;
    m_text->setText(text);
    for (QWidget *button : m_actionButtons) {
        button->deleteLater();
    }
    m_actionButtons.clear();
    for (const MessageAction &action : actions) {
        auto *button = new QToolButton(this);
        button->setText(action.label);
        button->setAccessibleName(action.label);
        connect(button, &QToolButton::clicked, this, [run = action.run] {
            run();
        });
        m_buttons->addWidget(button);
        m_actionButtons.push_back(button);
    }
    setVisible(true);
    update();
}

void InlineMessage::clear()
{
    QWidget::hide();
}

void InlineMessage::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_theme->color(backgroundToken(m_kind)));
    painter.fillRect(QRect(0, 0, barWidth, height()), m_theme->color(barToken(m_kind)));
}

} // namespace cc::ui
