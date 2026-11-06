// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

class QCheckBox;
class QSpinBox;

// Preferences > 영상 (ia.md section 7, video.md section 3): hardware decoding, the frame cache size and where
// extracted audio goes. The values are stored now; the decoder reads them once hardware decoding and the
// cache exist (video.md section 12), which the page says.
namespace cc::ui
{

class PathRow;
class Settings;
class Theme;

class VideoPage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    VideoPage(const Theme &theme, Settings &settings, QWidget *parent = nullptr);

    void restoreDefaults() override;

private:
    void bindControls();
    void syncFromSettings();

    Settings *m_settings;
    QCheckBox *m_hwDecode;
    QSpinBox *m_frameCache;
    PathRow *m_audioCacheDir;
    bool m_syncing = false;
};

} // namespace cc::ui
