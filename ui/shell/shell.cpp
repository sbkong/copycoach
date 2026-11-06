// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "ui/audio/audioservice.h"
#include "ui/copy/copybottomdock.h"
#include "ui/copy/copyinspector.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/routine/chordstrip.h"
#include "ui/routine/inspectorpanel.h"
#include "ui/routine/instrumentpanel.h"
#include "ui/routine/navigatorpanel.h"
#include "ui/routine/routinerecorder.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/routine/statsmodel.h"
#include "ui/routine/statspanel.h"
#include "ui/routine/timelineview.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/commandpalette.h"
#include "ui/shell/errortext.h"
#include "ui/shell/panelframe.h"
#include "ui/shell/settings.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/video/videopanel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QPixmapCache>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleHints>
#include <QVBoxLayout>

namespace cc::ui
{

namespace
{

constexpr int designWidth = 1280; // ia.md section 1: design 1280x720, hard minimum 1280x660
constexpr int designHeight = 720;
constexpr int hardMinimumHeight = 660;
constexpr int navigatorWidth = 220;
constexpr int inspectorWidth = 260;
constexpr double largeFontScale = 1.15; // qpalette.md section 4: font-size large = whole ramp x1.15

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QLabel *placeholder(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
}

Qt::DockWidgetArea areaFor(panel::Id id)
{
    switch (id) {
    case panel::Id::DockNavigator:
        return Qt::LeftDockWidgetArea;
    case panel::Id::DockInspector:
    case panel::Id::DockStats:
    case panel::Id::DockVideo:
        return Qt::RightDockWidgetArea;
    default:
        return Qt::BottomDockWidgetArea;
    }
}

} // namespace

Shell::Shell(const ShellContext &context, QWidget *parent)
    : QMainWindow(parent)
    , m_paths(context.paths)
    , m_theme(context.theme)
    , m_settings(context.settings)
    , m_actions(context.actions)
    , m_audio(context.audio)
    , m_session(context.session)
    , m_keymap(context.keymap)
    , m_stats(context.stats)
    , m_statsReader(context.statsReader)
    , m_icons(std::make_unique<Icons>(*context.theme))
    , m_central(new QStackedWidget(this))
    , m_jobs(context.jobs)
{
    setWindowTitle(QStringLiteral("CopyCoach"));
    setMinimumSize(designWidth, hardMinimumHeight);
    resize(designWidth, designHeight);
    setDockOptions(AnimatedDocks | AllowNestedDocks);
    setCentralWidget(m_central);
    setAcceptDrops(true); // files dropped anywhere import (shelldrop.cpp)
    for (QAction *action : m_actions->all()) {
        addAction(action); // window-level shortcuts
    }
    build();
}

void Shell::build()
{
    m_timer = new RoutineTimerController(*m_settings, *m_session, this);
    if (m_stats != nullptr) {
        new RoutineRecorder(*m_stats, *m_timer, *m_session, this); // owned by this
    }
    // After the recorder: both listen to the timer, and the model must read what the recorder just wrote.
    m_statsModel = new StatsModel(m_statsReader, *m_settings, *m_timer, this);
    m_palette = new CommandPalette(*m_theme, *m_actions, *m_settings, this);
    buildWorkspaces();
    buildDocks();
    buildMenus();
    buildToolbar();
    buildStatusBar();
    wireActions();
    markUnimplemented();
    applyTheme();
    connect(m_theme, &Theme::changed, this, &Shell::applyTheme);
    connect(m_settings, &Settings::valueChanged, this, &Shell::applyAppearanceSettings);
    if (!m_settings->windowGeometry().isEmpty()) {
        restoreGeometry(m_settings->windowGeometry());
    }
    switchTo(workspace::Id::Routine);
}

Shell::~Shell()
{
    // QMainWindow deletes the docks after our members are gone; their visibilityChanged must not call back.
    for (auto &[id, dock] : m_docks) {
        disconnect(dock, nullptr, this, nullptr);
    }
}

void Shell::buildWorkspaces()
{
    m_instrument = new InstrumentPanel(*m_theme, *m_settings, *m_session, this);
    m_timeline = new TimelineView(*m_theme, *m_session, this);
    auto *routineCentral = new QWidget(this);
    auto *layout = new QVBoxLayout(routineCentral);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ChordStrip(*m_theme, *m_actions, *m_session, routineCentral));
    layout->addWidget(m_instrument, 1);
    Workspace routine{workspace::Id::Routine, action::Id::WorkspaceRoutine, routineCentral, {}};
    routine.panels[panel::Id::DockNavigator] = new NavigatorPanel(*m_theme, *m_session, *m_timer, this);
    routine.panels[panel::Id::DockInspector] = new InspectorPanel(*m_theme, *m_settings, *m_session, this);
    routine.panels[panel::Id::DockTimeline] = m_timeline;
    routine.panels[panel::Id::DockStats] = new StatsPanel(*m_theme, *m_statsModel, *m_timer, this);
    m_workspaces.push_back(std::move(routine));

    m_editor = new ScoreEditor(this); // before the views that show it
    m_scoreView = new ScoreView(*m_theme, m_paths->verovioDir(), this);
    m_copyMessage = new InlineMessage(*m_theme, this);
    auto *copyCentral = new QWidget(this);
    auto *copyLayout = new QVBoxLayout(copyCentral);
    copyLayout->setContentsMargins(0, 0, 0, 0);
    copyLayout->setSpacing(0);
    copyLayout->addWidget(m_copyMessage);
    copyLayout->addWidget(m_scoreView, 1);
    Workspace copy{workspace::Id::Copy, action::Id::WorkspaceCopy, copyCentral, {}};
    m_copyDock = new CopyBottomDock(*m_theme, this);
    copy.panels[panel::Id::DockAudioTrack] = m_copyDock;
    copy.panels[panel::Id::DockNavigator] = placeholder(tr("프로젝트 · SETUP · LOOP · FINISH"), this);
    m_copyInspector = new CopyInspector(*m_theme, *m_editor, this);
    copy.panels[panel::Id::DockInspector] = m_copyInspector;
    m_videoPanel = new VideoPanel(*m_theme, *m_icons, this);
    copy.panels[panel::Id::DockVideo] = m_videoPanel;
    m_workspaces.push_back(std::move(copy));
    for (const Workspace &ws : m_workspaces) {
        m_central->addWidget(ws.central);
    }
}

QStackedWidget *Shell::stackFor(panel::Id id)
{
    auto *stack = new QStackedWidget(this);
    for (const Workspace &ws : m_workspaces) {
        const auto it = ws.panels.find(id);
        if (it != ws.panels.end()) {
            stack->addWidget(it->second);
        }
    }
    return stack;
}

void Shell::buildDocks()
{
    for (std::size_t i = 0; i < panel::infos.size(); ++i) {
        const auto id = static_cast<panel::Id>(i);
        if (id == panel::Id::ToolbarMain) {
            continue;
        }
        QStackedWidget *stack = stackFor(id);
        if (stack->count() == 0) {
            stack->deleteLater(); // panels of later pieces (stats, video, copy docks) are not created yet
            continue;
        }
        addDock(id, stack);
    }
}

void Shell::setDockVisible(panel::Id id, bool visible)
{
    const auto it = m_docks.find(id);
    if (it == m_docks.end()) {
        return;
    }
    // The view.dock.* actions are global, but a panel the active workspace does not own stays away (the copy
    // workspace has no stats or timeline dock); the action's check mark follows the dock's real state.
    if (!workspace(m_active).panels.contains(id)) {
        syncDockActions();
        return;
    }
    it->second->setVisible(visible);
}

void Shell::addDock(panel::Id id, QStackedWidget *stack)
{
    QDockWidget *dock = makeDock(fromView(panel::idOf(id)), fromView(panel::info(id).label), *m_theme, stack, this);
    if (id == panel::Id::DockNavigator) {
        dock->widget()->setMinimumWidth(navigatorWidth);
    } else if (id == panel::Id::DockInspector) {
        dock->widget()->setMinimumWidth(inspectorWidth);
    }
    placeDock(id, dock);
    connect(qobject_cast<PanelFrame *>(dock->widget())->header(),
            &PanelHeader::maximizeRequested,
            this,
            &Shell::maximizePanel);
    connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) {
        syncDockActions();
    });
    m_docks[id] = dock;
    m_dockStacks[id] = stack;
}

/// Default placement: the stats dock is a tab under the inspector (stats.md section 3); the rest by area.
void Shell::placeDock(panel::Id id, QDockWidget *dock)
{
    addDockWidget(areaFor(id), dock);
    const auto inspector = m_docks.find(panel::Id::DockInspector);
    if (id == panel::Id::DockStats && inspector != m_docks.end()) {
        tabifyDockWidget(inspector->second, dock);
        inspector->second->raise();
    }
}

panel::Id Shell::bottomPanel() const
{
    return m_active == workspace::Id::Copy ? panel::Id::DockAudioTrack : panel::Id::DockTimeline;
}

const Workspace &Shell::workspace(workspace::Id id) const
{
    for (const Workspace &ws : m_workspaces) {
        if (ws.id == id) {
            return ws;
        }
    }
    return m_workspaces.front();
}

void Shell::switchTo(workspace::Id id)
{
    if (m_maximized) {
        restorePanel();
    }
    if (!m_workspaces.empty() && m_active != id) {
        saveLayout();
    }
    m_active = id;
    const Workspace &ws = workspace(id);
    m_central->setCurrentWidget(ws.central);
    for (auto &[panelId, stack] : m_dockStacks) {
        const auto it = ws.panels.find(panelId);
        const bool owned = it != ws.panels.end();
        if (owned) {
            stack->setCurrentWidget(it->second);
        }
        m_docks[panelId]->setVisible(owned);
    }
    restoreLayout(id);
    updateVideoDock(); // hidden for projects without video, whatever the layout says
    for (const Workspace &each : m_workspaces) {
        m_actions->action(each.switchAction)->setChecked(each.id == id);
    }
    const auto context = id == workspace::Id::Copy ? workspace::Context::Copy : workspace::Context::Routine;
    m_actions->setActiveContext(context);
    if (id == workspace::Id::Copy) {
        pushCopyScore();
    } else {
        pushRoutineScore();
    }
    markUnimplemented();
    syncDockActions();
}

workspace::Id Shell::activeWorkspace() const
{
    return m_active;
}

bool Shell::isPanelMaximized() const
{
    return m_maximized;
}

void Shell::showStatus(const QString &message)
{
    statusBar()->showMessage(message);
}

void Shell::applyTheme()
{
    QApplication::setPalette(m_theme->palette(Surface::Canvas));
    QApplication::setFont(m_theme->font(theme::TypeStyle::Body));
    setPalette(m_theme->palette(Surface::Canvas));
    for (auto &[id, dock] : m_docks) {
        if (auto *frame = qobject_cast<PanelFrame *>(dock->widget())) {
            frame->applyTheme();
        }
    }
    QPixmapCache::clear(); // icons re-render with the new token colours
    for (QWidget *widget : QApplication::allWidgets()) {
        widget->update();
    }
}

/// Theme and font size follow the settings at runtime (qpalette.md section 5); dot size is the panel's job.
void Shell::applyAppearanceSettings(setting::Key key)
{
    if (key == setting::Key::AppearanceTheme) {
        reloadThemeFromSettings();
    } else if (key == setting::Key::AppearanceFontSize) {
        m_theme->setFontScale(m_settings->stringValue(key) == QStringLiteral("large") ? largeFontScale : 1.0);
    }
}

void Shell::reloadThemeFromSettings()
{
    if (m_paths == nullptr) {
        return;
    }
    const QString value = m_settings->stringValue(setting::Key::AppearanceTheme);
    const bool systemDark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    const bool dark = value == QStringLiteral("dark") || (value != QStringLiteral("light") && systemDark);
    if (dark == m_theme->isDark()) {
        return;
    }
    if (const auto r = m_theme->load(m_paths->theme(dark)); !r) {
        showStatus(errorText(r.error()));
    }
}

RoutineTimerController *Shell::timer() const
{
    return m_timer;
}

void Shell::showPreferences(int page)
{
    const PreferencesContext context{m_theme, m_settings, m_audio, m_actions, m_keymap, m_paths, m_jobs};
    PreferencesDialog dialog(context, this);
    dialog.showPage(static_cast<PreferencesPage>(page));
    dialog.exec();
}

void Shell::maximizePanel()
{
    if (m_maximized) {
        return;
    }
    m_beforeMaximize = saveState();
    for (auto &[id, dock] : m_docks) {
        dock->hide();
    }
    m_maximized = true;
    m_actions->action(action::Id::ViewPanelMaximize)->setChecked(true);
    m_instrument->fretboard()->setDotSize(DotSize::Large);
}

void Shell::restorePanel()
{
    if (!m_maximized) {
        return;
    }
    restoreState(m_beforeMaximize);
    m_maximized = false;
    m_actions->action(action::Id::ViewPanelMaximize)->setChecked(false);
    const bool large = m_settings->stringValue(setting::Key::AppearanceFretboardDotSize) == QStringLiteral("large");
    m_instrument->fretboard()->setDotSize(large ? DotSize::Large : DotSize::Normal);
    syncDockActions();
}

void Shell::saveLayout()
{
    if (!m_maximized) {
        m_settings->setLayoutState(m_active, saveState());
    }
    m_settings->setWindowGeometry(saveGeometry());
}

void Shell::restoreLayout(workspace::Id id)
{
    const QByteArray state = m_settings->layoutState(id);
    if (!state.isEmpty()) {
        restoreState(state);
    }
}

void Shell::syncDockActions()
{
    const auto sync = [this](action::Id actionId, panel::Id panelId) {
        const auto it = m_docks.find(panelId);
        if (it != m_docks.end()) {
            const QSignalBlocker blocker(m_actions->action(actionId));
            m_actions->action(actionId)->setChecked(!it->second->isHidden());
        }
    };
    sync(action::Id::ViewDockNavigator, panel::Id::DockNavigator);
    sync(action::Id::ViewDockInspector, panel::Id::DockInspector);
    sync(action::Id::ViewDockBottom, bottomPanel());
    sync(action::Id::ViewDockStats, panel::Id::DockStats);
    sync(action::Id::ViewDockVideo, panel::Id::DockVideo);
}

void Shell::resetLayout()
{
    for (const Workspace &ws : m_workspaces) {
        m_settings->setLayoutState(ws.id, QByteArray());
    }
    for (auto &[id, dock] : m_docks) {
        removeDockWidget(dock);
        placeDock(id, dock);
        dock->show();
    }
    switchTo(m_active);
}

void Shell::closeEvent(QCloseEvent *event)
{
    if (!confirmLoss()) {
        event->ignore();
        return;
    }
    // Pausing makes RoutineRecorder write the module stop before main ends the session; otherwise the seconds
    // since the last minute tick and the stop itself are lost on a normal exit (stats.md section 4).
    if (m_timer->timer().state() == routine::TimerState::Running) {
        m_timer->toggle();
    }
    saveLayout();
    QMainWindow::closeEvent(event);
}

} // namespace cc::ui
