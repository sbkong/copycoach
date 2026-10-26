// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QFrame>
#include <QString>

#include <functional>
#include <vector>

class QHBoxLayout;
class QLabel;

// Inline message (components.md section 2.1, states.md section 1): a colour bar, one sentence (cause -> next
// action), up to three action buttons and a close button. Replaces dialogs everywhere except the three allowed.
namespace cc::ui
{

class Theme;

enum class MessageKind : std::uint8_t {
    Info,
    Notice,
    Negative,
    Positive,
};

struct MessageAction {
    QString label;
    std::function<void()> run;
};

class InlineMessage final : public QFrame
{
    Q_OBJECT
public:
    explicit InlineMessage(const Theme &theme, QWidget *parent = nullptr);

    /// Shows the message; an empty `actions` list leaves only the close button. Hidden until shown.
    void show(MessageKind kind, const QString &text, const std::vector<MessageAction> &actions);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const Theme *m_theme;
    MessageKind m_kind = MessageKind::Info;
    QLabel *m_text;
    QHBoxLayout *m_buttons;
    std::vector<QWidget *> m_actionButtons;
};

} // namespace cc::ui
