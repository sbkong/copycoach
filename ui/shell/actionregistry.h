// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/actionids.h"
#include "generated/workspaceids.h"

#include <QAction>
#include <QObject>

#include <map>
#include <utility>
#include <vector>

// Every QAction in the app comes from the registry table (docs/ui/naming.md section 3): one per action id, and
// one per value for `{n}` templates. Menus, toolbars and the command palette only look actions up here.
namespace cc::ui
{

class ActionRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ActionRegistry(QObject *parent = nullptr);

    [[nodiscard]] QAction *action(action::Id id) const;
    /// Instance `n` of a template action such as routine.chord.select-{n}; nullptr outside the range.
    [[nodiscard]] QAction *action(action::Id id, int n) const;
    [[nodiscard]] const std::vector<QAction *> &all() const;

    /// Enables actions of the active workspace context plus global ones; other contexts are disabled, not
    /// hidden (components.md section 2.1 menu bar). The focus context is reset to Global.
    void setActiveContext(workspace::Context context);
    /// Adds the context of the focused widget (Editor while the score view has focus, Video for the video
    /// panel) on top of the workspace: its keys work only there. Global clears it.
    void setFocusContext(workspace::Context context);
    [[nodiscard]] workspace::Context activeContext() const;
    [[nodiscard]] workspace::Context focusContext() const;
    [[nodiscard]] static workspace::Context contextOf(action::Id id);

private:
    QAction *create(action::Id id, int n);
    void applyContexts();

    std::map<std::pair<action::Id, int>, QAction *> m_actions;
    std::vector<QAction *> m_all;
    workspace::Context m_active = workspace::Context::Global;
    workspace::Context m_focus = workspace::Context::Global;
};

} // namespace cc::ui
