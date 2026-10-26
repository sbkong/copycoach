// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QAbstractListModel>
#include <QFrame>
#include <QStringList>

#include <vector>

class QLineEdit;
class QListView;
class QLabel;

// Command palette (components.md section 2.1, mockup M9): a popup with a search field and every registry
// action, fuzzy-matched on label and id, recently run ones first. Disabled actions stay listed in grey with
// the reason (states.md section 6). Up / Down move, Enter runs, Esc closes.
namespace cc::ui
{

class ActionRegistry;
class Settings;
class Theme;

/// Rows = actions that match the query. Exposes label, context tag, key hint, enabled state and the reason.
class PaletteModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    static constexpr int contextRole = Qt::UserRole + 1; // "루틴", "보기" ...
    static constexpr int keyRole = Qt::UserRole + 2; // "Ctrl+E" or ""
    static constexpr int enabledRole = Qt::UserRole + 3;
    static constexpr int reasonRole = Qt::UserRole + 4;

    explicit PaletteModel(const ActionRegistry &actions, QObject *parent = nullptr);

    void setQuery(const QString &query);
    void setRecent(const QStringList &actionIds);
    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QAction *actionAt(int row) const;

    /// 0 = no match, 1 = letters in order, 2 = substring, 3 = prefix (case-insensitive).
    [[nodiscard]] static int matchScore(const QString &query, const QString &text);

private:
    void rebuild();

    const ActionRegistry *m_actions;
    QString m_query;
    QStringList m_recent;
    std::vector<QAction *> m_rows;
};

class CommandPalette final : public QFrame
{
    Q_OBJECT
public:
    static constexpr int maxRecent = 8;

    CommandPalette(const Theme &theme, const ActionRegistry &actions, Settings &settings, QWidget *parent);

    /// Shows the popup centred over the top of `parent`, query cleared, first row selected.
    void open();
    [[nodiscard]] PaletteModel *model() const;
    [[nodiscard]] QLineEdit *input() const;
    [[nodiscard]] QListView *list() const;
    /// Runs the current row when it is enabled; records it as recent; closes.
    void runCurrent();

    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void moveSelection(int delta);
    void rememberRecent(const QString &actionId);

    const Theme *m_theme;
    Settings *m_settings;
    PaletteModel *m_model;
    QLineEdit *m_input;
    QListView *m_list;
    QLabel *m_scope;
};

} // namespace cc::ui
