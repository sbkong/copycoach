// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"

#include <QWidget>

class QLineEdit;
class QPushButton;

namespace cc::ui
{

class Settings;

/// A folder setting on a preferences page: the value (empty = the app's default, shown as placeholder), a
/// browse button (the file dialog, one of the three allowed) and a clear button back to the default.
class PathRow final : public QWidget
{
    Q_OBJECT
public:
    PathRow(Settings &settings, setting::Key key, const QString &placeholder, QWidget *parent = nullptr);

    void syncFromSettings();

private:
    Settings *m_settings;
    setting::Key m_key;
    QLineEdit *m_edit;
    QPushButton *m_browse;
    QPushButton *m_clear;
    bool m_syncing = false;
};

} // namespace cc::ui
