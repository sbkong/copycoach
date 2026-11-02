// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "ui/prefs/preferencesdialog.h"

#include <filesystem>

class QCheckBox;
class QLabel;
class QSpinBox;

// Preferences > 기록 (ia.md section 7, stats.md section 6): collection on/off, the minimum minutes that make a
// day count, the retention period and where the database is. Applies immediately like the practice page;
// collection and the path take effect at the next start because the store is opened once at startup.
namespace cc::ui
{

class Settings;
class Theme;

class StatsPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    /// `userDir` is where stats.db lives when stats.db-path is empty (app/main.cpp openStats).
    StatsPage(const Theme &theme, Settings &settings, std::filesystem::path userDir, QWidget *parent = nullptr);

    void restoreDefaults() override;
    /// The path the next start will open: the setting, or <userDir>/stats.db when it is empty.
    [[nodiscard]] QString effectiveDbPath() const;

private:
    void bindControls();
    void syncFromSettings();

    Settings *m_settings;
    std::filesystem::path m_userDir;
    QCheckBox *m_enabled;
    QSpinBox *m_minDayMinutes;
    QSpinBox *m_retentionDays;
    QLabel *m_dbPath;
    bool m_syncing = false;
};

} // namespace cc::ui
