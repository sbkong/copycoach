// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/panelframe.h"

#include "ui/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace cc::ui
{

using theme::Spacing;
using theme::Token;

PanelChrome PanelChrome::flat()
{
    return PanelChrome{};
}

PanelHeader::PanelHeader(const QString &title, const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(title, this))
{
    auto *layout = new QHBoxLayout(this);
    const int edge = theme.spacing(Spacing::ContentArea);
    layout->setContentsMargins(edge, theme.spacing(Spacing::Unit), edge, theme.spacing(Spacing::Unit));
    m_label->setFont(theme.font(theme::TypeStyle::BodyStrong));
    layout->addWidget(m_label);
    layout->addStretch();
    setAccessibleName(title);
}

void PanelHeader::setTitle(const QString &title)
{
    m_label->setText(title);
}

void PanelHeader::mouseDoubleClickEvent(QMouseEvent * /*event*/)
{
    emit maximizeRequested();
}

PanelFrame::PanelFrame(const QString &title, const Theme &theme, QWidget *content, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_chrome(PanelChrome::flat())
    , m_header(new PanelHeader(title, theme, this))
    , m_content(content)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    content->setParent(this);
    layout->addWidget(content, 1);
    applyTheme();
}

PanelHeader *PanelFrame::header() const
{
    return m_header;
}

QWidget *PanelFrame::content() const
{
    return m_content;
}

void PanelFrame::applyTheme()
{
    setPalette(m_theme->palette(Surface::Panel));
    setAutoFillBackground(false); // paintEvent fills, so the radius can apply later
    update();
}

void PanelFrame::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    const QColor fill = m_theme->color(Token::BackgroundLayer1Color);
    if (m_chrome.radius > 0) {
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_chrome.radius, m_chrome.radius);
        painter.fillPath(path, fill);
        return;
    }
    painter.fillRect(rect(), fill);
    if (m_chrome.border) {
        painter.setPen(m_theme->color(Token::BorderColorDefault));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

QDockWidget *
makeDock(const QString &panelId, const QString &title, const Theme &theme, QWidget *content, QWidget *parent)
{
    auto *dock = new QDockWidget(title, parent);
    dock->setObjectName(panelId);
    auto *frame = new PanelFrame(title, theme, content, dock);
    dock->setTitleBarWidget(new QWidget(dock)); // the frame paints its own header
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    dock->setWidget(frame);
    return dock;
}

} // namespace cc::ui
