// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/actionregistry.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace cc::ui
{

namespace
{

constexpr int noInstance = 0;

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

/// Registry labels are the Korean source strings; the translation catalog keys on them (coding-standards 22).
QString translated(std::string_view label)
{
    const QString source = fromView(label);
    return QCoreApplication::translate("actions", source.toUtf8().constData());
}

QString expand(std::string_view text, int n)
{
    return fromView(text).replace(QStringLiteral("{n}"), QString::number(n));
}

bool isTemplate(const action::Info &info)
{
    return info.rangeMax > 0;
}

} // namespace

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
{
    for (std::size_t i = 0; i < action::infos.size(); ++i) {
        const auto id = static_cast<action::Id>(i);
        const action::Info &info = action::info(id);
        if (!isTemplate(info)) {
            create(id, noInstance);
            continue;
        }
        for (int n = info.rangeMin; n <= info.rangeMax; ++n) {
            create(id, n);
        }
    }
}

QAction *ActionRegistry::create(action::Id id, int n)
{
    const action::Info &info = action::info(id);
    auto *action = new QAction(this);
    action->setObjectName(expand(info.id, n));
    action->setText(n == noInstance ? translated(info.label) : expand(info.label, n));
    QList<QKeySequence> keys;
    for (std::size_t k = 0; k < info.keyCount; ++k) {
        const QString text = expand(info.defaultKeys[k], n);
        if (!text.isEmpty()) {
            keys.append(QKeySequence::fromString(text, QKeySequence::PortableText));
        }
    }
    action->setShortcuts(keys);
    action->setShortcutContext(Qt::WindowShortcut);
    m_actions[{id, n}] = action;
    m_all.push_back(action);
    return action;
}

QAction *ActionRegistry::action(action::Id id) const
{
    return action(id, noInstance);
}

QAction *ActionRegistry::action(action::Id id, int n) const
{
    const auto it = m_actions.find({id, n});
    return it == m_actions.end() ? nullptr : it->second;
}

const std::vector<QAction *> &ActionRegistry::all() const
{
    return m_all;
}

workspace::Context ActionRegistry::contextOf(action::Id id)
{
    const std::string_view context = action::info(id).context;
    for (std::size_t i = 0; i < workspace::contextIds.size(); ++i) {
        if (workspace::contextIds[i] == context) {
            return static_cast<workspace::Context>(i);
        }
    }
    return workspace::Context::Global;
}

void ActionRegistry::setActiveContext(workspace::Context context)
{
    m_active = context;
    m_focus = workspace::Context::Global;
    applyContexts();
}

void ActionRegistry::setFocusContext(workspace::Context context)
{
    if (m_focus == context) {
        return;
    }
    m_focus = context;
    applyContexts();
}

workspace::Context ActionRegistry::activeContext() const
{
    return m_active;
}

workspace::Context ActionRegistry::focusContext() const
{
    return m_focus;
}

void ActionRegistry::applyContexts()
{
    for (const auto &[key, action] : m_actions) {
        const workspace::Context own = contextOf(key.first);
        action->setEnabled(own == workspace::Context::Global || own == m_active || own == m_focus);
    }
}

} // namespace cc::ui
