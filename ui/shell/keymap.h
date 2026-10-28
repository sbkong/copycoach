// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "generated/actionids.h"
#include "generated/workspaceids.h"

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

// keymap.json (docs/ui/naming.md section 5): { "schema_version": 1, "preset": "gp8", "keys": { "<action id>":
// ["Ctrl+N"] } }. Only keys that differ from the registry default are stored. Applies to the ActionRegistry.
namespace cc::ui
{

class ActionRegistry;

struct KeyConflict {
    QString actionId; // the other action holding the same key
    QString label;
    QKeySequence key;
};

class Keymap : public QObject
{
    Q_OBJECT
public:
    explicit Keymap(std::filesystem::path dir, QObject *parent = nullptr);

    [[nodiscard]] Result<void> load();
    [[nodiscard]] Result<void> save() const;

    /// Registry default for an action id (template instances expanded, e.g. "routine.chord.select-3").
    [[nodiscard]] static QList<QKeySequence> defaultKeys(const QString &actionId);
    [[nodiscard]] QList<QKeySequence> keys(const QString &actionId) const;
    [[nodiscard]] bool isDefault(const QString &actionId) const;
    [[nodiscard]] int overrideCount() const;

    /// Stores (or clears, when equal to the default) and emits keysChanged.
    void setKeys(const QString &actionId, const QList<QKeySequence> &keys);
    void resetAll();

    /// Other actions in the same context (or global vs any context) already using `key`.
    [[nodiscard]] std::vector<KeyConflict> conflicts(const QString &actionId, const QKeySequence &key) const;

    /// Pushes every action's keys into its QAction.
    void apply(ActionRegistry &registry) const;

signals:
    void keysChanged(const QString &actionId);

private:
    void collectConflicts(const action::Info &info,
                          const QString &actionId,
                          const QKeySequence &key,
                          std::vector<KeyConflict> &found) const;

    std::filesystem::path m_dir;
    std::map<QString, QList<QKeySequence>> m_overrides;
};

/// Context of an action id (a routine.* id -> Routine); global for unknown ids.
[[nodiscard]] workspace::Context contextOfActionId(const QString &actionId);

} // namespace cc::ui
