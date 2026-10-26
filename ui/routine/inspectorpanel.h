// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "generated/settingkeys.h"

#include <QWidget>

#include <map>
#include <string>
#include <vector>

class QFormLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;

// Routine inspector dock (components.md section 2.4): fretboard display segments (label / colour / root), module
// options generated from the mode definition, the 12 practice keys, and the selected chord's properties.
namespace cc::ui
{

class RoutineSession;
class Settings;
class Theme;

class InspectorPanel final : public QWidget
{
    Q_OBJECT
public:
    InspectorPanel(const Theme &theme, Settings &settings, RoutineSession &session, QWidget *parent = nullptr);

private:
    struct Segment {
        std::vector<QToolButton *> buttons;
        std::vector<std::string> values;
    };

    void buildDisplayGroup();
    void buildDisplaySegment(setting::Key key, const QString &title);
    void buildOptionGroups();
    void clearOptionGroups();
    void addOptionSegment(const std::string &name, const std::vector<std::string> &values);
    void buildKeyGrid();
    void syncDisplay();
    void syncOptions();
    void syncKeys();
    void syncSelectedChord();
    Segment addSegment(QVBoxLayout *into,
                       const QString &title,
                       const std::vector<std::string> &values,
                       const std::vector<QString> &labels);

    const Theme *m_theme;
    Settings *m_settings;
    RoutineSession *m_session;
    QVBoxLayout *m_layout;
    QWidget *m_optionsHost;
    std::map<setting::Key, Segment> m_display;
    std::map<std::string, Segment> m_options;
    std::vector<QToolButton *> m_keyButtons;
    QLabel *m_chordTitle;
    QFormLayout *m_chordForm;
    std::vector<QLabel *> m_chordValues;
};

} // namespace cc::ui
