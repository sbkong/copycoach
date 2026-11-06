// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QSpinBox;

// Preferences > 악보·탭 (ia.md section 7): the defaults a new track or project starts from (tuning, capo,
// notation, instrument view mode, cursor follow, fingering preset). Applies immediately; per-song values
// live in the track inspector.
namespace cc::ui
{

class Settings;
class Theme;

class ScoreTabPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    ScoreTabPage(const Theme &theme, Settings &settings, QWidget *parent = nullptr);

    void restoreDefaults() override;

private:
    void buildForm(QFormLayout *form);
    void bindEnumCombo(QComboBox *combo, setting::Key key);
    void syncFromSettings();

    Settings *m_settings;
    QComboBox *m_tuning;
    QSpinBox *m_capo;
    QComboBox *m_notation;
    QComboBox *m_instrumentMode;
    QCheckBox *m_followCursor;
    QComboBox *m_fingeringPreset;
    bool m_syncing = false;
};

} // namespace cc::ui
