// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

class QCheckBox;
class QSpinBox;

// Preferences > 저장·파일 (ia.md section 7, states.md section 5): autosave interval, the default project
// folder, the recent list length, copying imported media into the project. Applies immediately (the
// autosave timer restarts on the new interval).
namespace cc::ui
{

class PathRow;
class Settings;
class Theme;

class FilesPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    FilesPage(const Theme &theme, Settings &settings, QWidget *parent = nullptr);

    void restoreDefaults() override;

private:
    void bindControls();
    void syncFromSettings();

    Settings *m_settings;
    QSpinBox *m_autosave;
    PathRow *m_projectDir;
    QSpinBox *m_recentCount;
    QCheckBox *m_copyMedia;
    bool m_syncing = false;
};

} // namespace cc::ui
