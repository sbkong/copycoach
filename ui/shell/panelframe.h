// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QDockWidget>
#include <QWidget>

class QLabel;

// Dock panel chrome (spike (d), components.md section 2.1): PanelChrome holds the only values that differ
// between the flat look and the JetBrains Islands look; PanelHeader is the custom title bar (title, maximize);
// PanelFrame paints the panel background and hosts the header + content. Islands stay on hold (TODO.md), so
// the chrome is flat for now but nothing outside this file knows.
namespace cc::ui
{

class Theme;

struct PanelChrome {
    int gap = 0;
    int radius = 0;
    bool border = true;

    [[nodiscard]] static PanelChrome flat();
};

class PanelHeader final : public QWidget
{
    Q_OBJECT
public:
    PanelHeader(const QString &title, const Theme &theme, QWidget *parent = nullptr);

    void setTitle(const QString &title);

signals:
    void maximizeRequested();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QLabel *m_label;
};

class PanelFrame final : public QWidget
{
    Q_OBJECT
public:
    PanelFrame(const QString &title, const Theme &theme, QWidget *content, QWidget *parent = nullptr);

    [[nodiscard]] PanelHeader *header() const;
    [[nodiscard]] QWidget *content() const;
    void applyTheme();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const Theme *m_theme;
    PanelChrome m_chrome;
    PanelHeader *m_header;
    QWidget *m_content;
};

/// A QDockWidget whose objectName is the panel id and whose title bar is a PanelHeader.
QDockWidget *
makeDock(const QString &panelId, const QString &title, const Theme &theme, QWidget *content, QWidget *parent);

} // namespace cc::ui
