// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QWidget>

#include <array>

class QLabel;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

// Routine navigator dock (components.md section 2.2): module tree (module -> modes), today's assignment, and the
// per-module progress rows. The tree mirrors RoutineDefinition; the selection follows the session.
namespace cc::ui
{

class RoutineSession;
class RoutineTimerController;
class Theme;

class NavigatorPanel final : public QWidget
{
    Q_OBJECT
public:
    NavigatorPanel(const Theme &theme,
                   RoutineSession &session,
                   RoutineTimerController &timer,
                   QWidget *parent = nullptr);

private:
    void fillTree();
    void syncSelection();
    [[nodiscard]] QStandardItem *findModeItem(const QString &module, const QString &mode) const;
    void updateToday();
    void buildProgress();
    void updateProgress();

    const Theme *m_theme;
    RoutineSession *m_session;
    QStandardItemModel *m_model;
    QTreeView *m_tree;
    QLabel *m_today;
    RoutineTimerController *m_timer;
    std::array<QLabel *, 4> m_progress{};
};

} // namespace cc::ui
