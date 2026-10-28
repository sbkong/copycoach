// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/workspaceids.h"
#include "ui/prefs/preferencesdialog.h"

#include <QAbstractTableModel>
#include <QKeySequence>

#include <vector>

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QTreeView;
class QVBoxLayout;

// Preferences > shortcuts (components.md section 2.8, mockup M8b): preset state, search, one tab per context,
// a table of action / key / source, a key recorder, per-item and whole-keymap defaults. Changes apply at once
// (QAction shortcuts) and keymap.json is written; a same-context conflict is shown inline and not saved.
namespace cc::ui
{

class ActionRegistry;
class InlineMessage;
class Keymap;
class Theme;
struct KeyConflict;

class KeymapModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    static constexpr int actionColumn = 0;
    static constexpr int keyColumn = 1;
    static constexpr int sourceColumn = 2;
    static constexpr int idRole = Qt::UserRole + 1;
    static constexpr int conflictRole = Qt::UserRole + 2;

    KeymapModel(const ActionRegistry &actions, const Keymap &keymap, const Theme &theme, QObject *parent = nullptr);

    void setFilter(workspace::Context context, const QString &query);
    void setConflictRow(const QString &actionId); // key cell painted negative until cleared
    [[nodiscard]] QString actionIdAt(int row) const;
    [[nodiscard]] int rowOf(const QString &actionId) const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void rebuild();
    [[nodiscard]] QVariant sourceText(const QString &actionId) const;

    const ActionRegistry *m_actions;
    const Keymap *m_keymap;
    const Theme *m_theme;
    workspace::Context m_context = workspace::Context::Global;
    QString m_query;
    QString m_conflictId;
    std::vector<QString> m_rows;
};

class ShortcutsPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    ShortcutsPage(const Theme &theme, ActionRegistry &actions, Keymap &keymap, QWidget *parent = nullptr);

    void restoreDefaults() override;

    /// Sets the key of the selected row; false (with the inline conflict message) when another action has it.
    bool assignKey(const QKeySequence &key);
    void selectAction(const QString &actionId);
    [[nodiscard]] KeymapModel *model() const;
    [[nodiscard]] QString selectedActionId() const;

private:
    void buildHeader(QVBoxLayout *layout);
    void buildRecorder(QVBoxLayout *layout);
    void refilter();
    void updatePresetLabel();
    void showConflict(const KeyConflict &conflict, const QString &actionId, const QKeySequence &previous);
    void resetSelected();

    const Theme *m_theme;
    ActionRegistry *m_actions;
    Keymap *m_keymap;
    KeymapModel *m_model;
    QLabel *m_preset;
    QLineEdit *m_search;
    QTabBar *m_tabs;
    QTreeView *m_table;
    QKeySequenceEdit *m_recorder;
    QPushButton *m_assign;
    QPushButton *m_itemDefault;
    InlineMessage *m_message;
};

} // namespace cc::ui
