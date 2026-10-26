// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/backend.h"
#include "ui/prefs/preferencesdialog.h"

#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QToolButton;

// Preferences > audio device (PLAN 4-1 screen, mockup M8): host API, driver or input / output devices, sample
// rate, buffer, channel roles, reported latency, test tone. Edits stay local until Apply reopens the stream.
namespace cc::ui
{

class AudioService;
class InlineMessage;
class Settings;
class Theme;

class AudioDevicePage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    AudioDevicePage(const Theme &theme, Settings &settings, AudioService &audio, QWidget *parent = nullptr);

    void restoreDefaults() override;
    [[nodiscard]] bool needsApply() const override;
    void apply() override;
    void discard() override;

    [[nodiscard]] QString hostApi() const;
    [[nodiscard]] bool hasMessage() const;

private:
    void buildSystemRow(QFormLayout *form);
    void buildDeviceRows(QFormLayout *form);
    void buildFormatRow(QFormLayout *form);
    void buildLatencyRow(QFormLayout *form);
    void loadFromSettings();
    void refreshDevices();
    void fillDeviceCombo(QComboBox *combo, bool input, const QString &current);
    void syncRows();
    void showDeviceState();
    void showError(const Error &error);
    void openControlPanel();

    const Theme *m_theme;
    Settings *m_settings;
    AudioService *m_audio;
    std::vector<audio::DeviceInfo> m_devices;
    std::vector<QToolButton *> m_systemButtons;
    QComboBox *m_driver;
    QPushButton *m_controlPanel;
    QComboBox *m_input;
    QComboBox *m_output;
    QComboBox *m_sampleRate;
    QComboBox *m_buffer;
    QLabel *m_bufferNote;
    QLabel *m_latency;
    QPushButton *m_testTone;
    QPushButton *m_refresh;
    InlineMessage *m_message;
    QWidget *m_driverRow = nullptr;
    QWidget *m_inputRow = nullptr;
    QWidget *m_outputRow = nullptr;
};

} // namespace cc::ui
