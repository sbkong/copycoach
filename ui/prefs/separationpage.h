// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

#include <filesystem>
#include <optional>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;

// Preferences > 스템 분리 (ia.md section 7, states.md section 2): the models on this machine (the Beat This!
// files with a download button; the stem model until separation exists is a plain note), the accelerator,
// the model and cache folders and the cache limit. Applies immediately.
namespace cc::ui
{

class JobService;
class PathRow;
class Settings;
class Theme;

class SeparationPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    /// `paths` is where the models live (nullopt in tests without an app folder); `jobs` runs the download
    /// (null = the button is disabled).
    SeparationPage(const Theme &theme,
                   Settings &settings,
                   std::optional<std::filesystem::path> modelsDir,
                   JobService *jobs,
                   QWidget *parent = nullptr);

    void restoreDefaults() override;
    /// "있음 · final · small" / "없음" / "내려받는 중": what the status label shows.
    [[nodiscard]] QString beatModelStatus() const;

private:
    void buildModelRows(QFormLayout *form);
    void bindCombo(QComboBox *combo, setting::Key key);
    void bindControls();
    void syncFromSettings();
    void refreshModelStatus();
    void downloadBeatModels();

    Settings *m_settings;
    std::optional<std::filesystem::path> m_modelsDir;
    JobService *m_jobs;
    QLabel *m_beatStatus;
    QPushButton *m_download;
    QComboBox *m_beatModel;
    QComboBox *m_accelerator;
    PathRow *m_modelDir;
    PathRow *m_cacheDir;
    QSpinBox *m_cacheLimit;
    bool m_downloading = false;
    bool m_syncing = false;
};

} // namespace cc::ui
