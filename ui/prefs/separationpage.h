// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/predict/modelfile.h"
#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

#include <QUrl>

#include <filesystem>
#include <vector>

class QComboBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSpinBox;

// Preferences > 스템 분리 (ia.md section 7, states.md section 2): the models on this machine (the Spleeter
// file and the Beat This! files, each with a download button; the user's research-only htdemucs file when
// present), the accelerator, the model and cache folders and the cache limit. Applies immediately.
namespace cc::ui
{

class JobService;
struct AppPaths;
class PathRow;
class Settings;
class Theme;

class SeparationPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    /// `paths` is where the models live (null in tests without an app folder); `jobs` runs the downloads
    /// (null = the buttons are disabled).
    SeparationPage(const Theme &theme,
                   Settings &settings,
                   const AppPaths *paths,
                   JobService *jobs,
                   QWidget *parent = nullptr);
    void restoreDefaults() override;
    /// "있음 · final · small" / "없음" / "내려받는 중": what the status label shows.
    [[nodiscard]] QString beatModelStatus() const;
    /// "있음 · Spleeter" / "연구용 htdemucs 있음 · Spleeter 없음" / "없음": the stem model row.
    [[nodiscard]] QString stemModelStatus() const;

private:
    void buildModelRows(QFormLayout *form);
    void bindCombo(QComboBox *combo, setting::Key key);
    void bindControls();
    void syncFromSettings();
    void refreshModelStatus();
    void refreshStemStatus();
    void downloadBeatModels();
    void downloadStemModel();
    void startDownload(const QUrl &base, const std::filesystem::path &dir, std::vector<predict::ModelFile> missing);

    Settings *m_settings;
    const AppPaths *m_paths;
    JobService *m_jobs;
    QLabel *m_beatStatus;
    QPushButton *m_download;
    QLabel *m_stemStatus;
    QPushButton *m_stemDownload;
    QComboBox *m_beatModel;
    QComboBox *m_accelerator;
    PathRow *m_modelDir;
    PathRow *m_cacheDir;
    QSpinBox *m_cacheLimit;
    bool m_downloading = false;
    bool m_syncing = false;
};

} // namespace cc::ui
