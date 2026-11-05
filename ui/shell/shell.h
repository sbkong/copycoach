// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/command.h"
#include "core/jobs/job.h"
#include "generated/actionids.h"
#include "generated/panelids.h"
#include "generated/settingkeys.h"
#include "generated/workspaceids.h"
#include "ui/shell/apppaths.h"

#include <QMainWindow>

#include <filesystem>
#include <span>

#include <map>
#include <memory>
#include <vector>

namespace cc::stats
{
class IStatsSink;
class IStatsReader;
} // namespace cc::stats

class QComboBox;
class QMenu;
class QDockWidget;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QToolBar;
class QToolButton;

// The application window (ia.md section 1): menu bar and toolbar built from the action registry, one dock per
// panel id whose content switches with the workspace, status bar, panel maximize / restore, and the layout
// saved per workspace. The shell is a workspace list (naming.md section 4): no routine / copy branches.
namespace cc::ui
{

class ActionRegistry;
class AudioService;
class CommandPalette;
class Keymap;
class Icons;
class InlineMessage;
class InstrumentPanel;
class JobService;
class JobsChip;
class RoutineSession;
class ProjectSession;
class RoutineTimerController;
class ScoreEditor;
class ScoreView;
class Settings;
class StatsModel;
class Theme;
class TimelineView;
/// Everything the shell talks to; owned by the composition root (app/main.cpp), outlives the window.
struct ShellContext {
    const AppPaths *paths = nullptr;
    Theme *theme = nullptr;
    Settings *settings = nullptr;
    ActionRegistry *actions = nullptr;
    AudioService *audio = nullptr;
    RoutineSession *session = nullptr;
    Keymap *keymap = nullptr;
    stats::IStatsSink *stats = nullptr; // optional: no recorder without it (tests)
    const stats::IStatsReader *statsReader = nullptr; // optional: the stats dock shows its empty state without it
    JobService *jobs = nullptr; // optional: no jobs chip without it
};

struct Workspace {
    workspace::Id id;
    action::Id switchAction;
    QWidget *central = nullptr;
    std::map<panel::Id, QWidget *> panels; // content shown in that panel while the workspace is active
};

class Shell final : public QMainWindow
{
    Q_OBJECT
public:
    explicit Shell(const ShellContext &context, QWidget *parent = nullptr);
    ~Shell() override;
    Q_DISABLE_COPY_MOVE(Shell)

    void switchTo(workspace::Id id);
    [[nodiscard]] workspace::Id activeWorkspace() const;
    [[nodiscard]] bool isPanelMaximized() const;
    void showStatus(const QString &message);
    /// Opens the preferences dialog (modal) on a page; the status bar device chip opens the audio page.
    void showPreferences(int page);
    [[nodiscard]] RoutineTimerController *timer() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // shellmenus.cpp
    void buildMenus();
    void buildToolbar();
    void buildToolbarControls(QToolBar *bar);
    void buildStatusBar();
    void wireActions();
    void wireRoutineActions();
    void wireRoutineNavigation();
    void wireTempoAndKey();
    void wireTempo();
    void wireKey();
    void wireDockActions();
    void wireViewActions();
    void wireSettingActions();
    void wireTimerActions();
    void updateTimerChip();
    void updateTodayMinutes();
    void showJobResult(const jobs::JobEvent &event);
    void importScore(); // file.import: MusicXML into the copy workspace (audio and video follow in 4c)
    void pushCopyScore(); // the loaded score, its tempo and meter into the engine (synth playback)
    void pushRoutineScore();
    // shellproject.cpp
    void wireProjectActions();
    [[nodiscard]] bool confirmLoss(); // the loss dialog; false = the user cancelled
    [[nodiscard]] std::filesystem::path defaultProjectDir() const;
    void newProject();
    void openProject();
    void openProjectFolder(const std::filesystem::path &folder);
    void openRecent(const std::filesystem::path &folder);
    void markCheckable();
    void fillMenu(QMenu *menu, std::span<const action::Id> items);
    bool saveProject();
    bool saveProjectAs();
    void offerRecovery(const QString &savedAt);
    void updateProjectTitle();
    void updateAutosaveChip();
    void rebuildRecentMenu();
    // shelleditor.cpp
    void wireEditorActions();
    void wireEditorToView();
    void updateEditorLabel();
    void showTransportState(audio::EngineState state);
    void markUnimplemented();
    // shell.cpp
    void build();
    void buildWorkspaces();
    void buildDocks();
    [[nodiscard]] QStackedWidget *stackFor(panel::Id id);
    void setDockVisible(panel::Id id, bool visible);
    void addDock(panel::Id id, QStackedWidget *stack);
    void placeDock(panel::Id id, QDockWidget *dock);
    void applyTheme();
    void applyAppearanceSettings(setting::Key key);
    void reloadThemeFromSettings();
    void maximizePanel();
    void restorePanel();
    void saveLayout();
    void restoreLayout(workspace::Id id);
    void syncDockActions();
    void resetLayout();
    [[nodiscard]] const Workspace &workspace(workspace::Id id) const;

    const AppPaths *m_paths;
    Theme *m_theme;
    Settings *m_settings;
    ActionRegistry *m_actions;
    AudioService *m_audio;
    RoutineSession *m_session;
    Keymap *m_keymap;
    stats::IStatsSink *m_stats;
    const stats::IStatsReader *m_statsReader;
    std::unique_ptr<Icons> m_icons;
    std::vector<Workspace> m_workspaces;
    workspace::Id m_active = workspace::Id::Routine;
    std::map<panel::Id, QDockWidget *> m_docks;
    std::map<panel::Id, QStackedWidget *> m_dockStacks;
    QStackedWidget *m_central = nullptr;
    InstrumentPanel *m_instrument = nullptr;
    RoutineTimerController *m_timer = nullptr;
    StatsModel *m_statsModel = nullptr;
    QToolButton *m_timerChip = nullptr;
    QLabel *m_todayLabel = nullptr;
    CommandPalette *m_palette = nullptr;
    TimelineView *m_timeline = nullptr;
    QSpinBox *m_tempo = nullptr;
    QComboBox *m_key = nullptr;
    QToolButton *m_deviceChip = nullptr;
    JobsChip *m_jobsChip = nullptr;
    ScoreView *m_scoreView = nullptr;
    ScoreEditor *m_editor = nullptr;
    ProjectSession *m_project = nullptr;
    InlineMessage *m_copyMessage = nullptr;
    QLabel *m_autosaveLabel = nullptr;
    QMenu *m_recentMenu = nullptr;
    JobService *m_jobs = nullptr;
    QLabel *m_contextLabel = nullptr;
    QByteArray m_beforeMaximize;
    bool m_maximized = false;
};

} // namespace cc::ui
