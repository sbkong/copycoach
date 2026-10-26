// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"
#include "ui/prefs/preferencesdialog.h"

#include <map>

class QFormLayout;
class QToolButton;
#include <string>
#include <vector>

class QFormLayout;
class QToolButton;

// Preferences > appearance (ia.md section 7): theme, fretboard dot size, font size. Segments apply immediately.
namespace cc::ui
{

class Settings;
class Theme;

class AppearancePage final : public PreferencesPageBase
{
    Q_OBJECT
public:
    AppearancePage(const Theme &theme, Settings &settings, QWidget *parent = nullptr);

    void restoreDefaults() override;

private:
    struct Segment {
        std::vector<QToolButton *> buttons;
        std::vector<std::string> values;
    };
    void
    addSegment(QFormLayout *form, setting::Key key, const QString &title, const std::map<std::string, QString> &labels);
    void syncFromSettings();

    const Theme *m_theme;
    Settings *m_settings;
    std::map<setting::Key, Segment> m_segments;
};

} // namespace cc::ui
