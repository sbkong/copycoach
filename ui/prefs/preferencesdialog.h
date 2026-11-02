// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QDialog>

#include <cstdint>
#include <vector>

class QListWidget;
class QPushButton;
class QStackedWidget;

class QListWidget;
class QPushButton;
class QStackedWidget;

// Preferences (ia.md section 7, components.md section 2.8): page list on the left, one page on the right.
// Values apply immediately, except the audio device page, which has Apply / Cancel because the stream restarts.
// One of the three dialogs the app allows (ia.md section 5).
namespace cc::ui
{

class ActionRegistry;
struct AppPaths;
class AudioService;
class Keymap;
class Settings;
class Theme;

/// What the pages talk to; all owned by the composition root.
struct PreferencesContext {
    const Theme *theme = nullptr;
    Settings *settings = nullptr;
    AudioService *audio = nullptr;
    ActionRegistry *actions = nullptr;
    Keymap *keymap = nullptr;
    const AppPaths *paths = nullptr; // optional: the stats page shows where stats.db lives
};

enum class PreferencesPage : std::uint8_t {
    AudioDevice,
    Appearance,
    Practice,
    ScoreTab,
    StemSeparation,
    Shortcuts,
    Files,
    Stats,
    Video,
    Count,
};

/// What every page provides: its widget, "restore defaults", and whether it needs Apply (audio only).
class PreferencesPageBase : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;

    virtual void restoreDefaults() = 0;
    /// Pages that apply on Apply (not immediately) return true; the dialog shows the buttons for them.
    [[nodiscard]] virtual bool needsApply() const;
    virtual void apply();
    virtual void discard();
};

class PreferencesDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(const PreferencesContext &context, QWidget *parent = nullptr);

    void showPage(PreferencesPage page);
    [[nodiscard]] PreferencesPage currentPage() const;
    [[nodiscard]] PreferencesPageBase *page(PreferencesPage page) const;

private:
    void buildLayout();
    void addPage(PreferencesPage id, const QString &title, PreferencesPageBase *widget);
    void syncButtons();

    QListWidget *m_list;
    QStackedWidget *m_stack;
    QPushButton *m_restore;
    QPushButton *m_cancel;
    QPushButton *m_apply;
    std::vector<PreferencesPageBase *> m_pages;
};

} // namespace cc::ui
