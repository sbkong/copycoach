// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

#include <array>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QSpinBox;

class QCheckBox;
class QComboBox;
class QFormLayout;
class QSpinBox;

// Preferences > practice (ia.md section 7, routine-spec.md section 2.9): count-in, default sounds, timer
// minutes, default tempo and time signature, weekday auto-assign, session autosave. Applies immediately.
namespace cc::ui
{

class AudioService;
class Settings;
class Theme;

class PracticePage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    PracticePage(const Theme &theme, Settings &settings, AudioService &audio, QWidget *parent = nullptr);

    void restoreDefaults() override;

private:
    void buildSoundRows(QFormLayout *form);
    void bindSoundCombo(QComboBox *combo, setting::Key key);
    void buildTimerRow(QFormLayout *form);
    void saveTimerMinutes();
    void bindSimpleControls();
    void syncFromSettings();
    void fillSoundCombo(QComboBox *combo, int kind);

    const Theme *m_theme;
    Settings *m_settings;
    AudioService *m_audio;
    QSpinBox *m_countIn;
    QComboBox *m_metronome;
    QComboBox *m_guitar;
    QComboBox *m_piano;
    std::array<QSpinBox *, 4> m_timer{};
    QSpinBox *m_tempo;
    QComboBox *m_timeSig;
    QCheckBox *m_autoAssign;
    QCheckBox *m_autosave;
    bool m_syncing = false;
};

} // namespace cc::ui
