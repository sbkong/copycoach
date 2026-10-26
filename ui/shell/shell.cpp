// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "ui/audio/audioservice.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/routine/chordstrip.h"
#include "ui/routine/inspectorpanel.h"
#include "ui/routine/instrumentpanel.h"
#include "ui/routine/navigatorpanel.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/timelineview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"
#include "ui/shell/panelframe.h"
#include "ui/shell/settings.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

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
    , m_icons(std::make_unique<Icons>(*context.theme))
    , m_central(new QStackedWidget(this))
{
    setWindowTitle(QStringLiteral("CopyCoach"));
    setMinimumSize(designWidth, hardMinimumHeight);
    resize(designWidth, designHeight);
    setDockOptions(AnimatedDocks | AllowNestedDocks);
    setCentralWidget(m_central);
    for (QAction *action : m_actions->all()) {
        addAction(action); // window-level shortcuts
    }
    build();
}

void Shell::build()
{
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
    routine.panels[panel::Id::DockNavigator] = new NavigatorPanel(*m_theme, *m_session, this);
    routine.panels[panel::Id::DockInspector] = new InspectorPanel(*m_theme, *m_settings, *m_session, this);
    routine.panels[panel::Id::DockTimeline] = m_timeline;
    m_workspaces.push_back(std::move(routine));

    Workspace copy{workspace::Id::Copy,
                   action::Id::WorkspaceCopy,
                   placeholder(tr("카피 작업공간은 재설계 뒤에 채워집니다."), this),
                   {}};
    copy.panels[panel::Id::DockNavigator] = placeholder(tr("프로젝트 · SETUP · LOOP · FINISH"), this);
    copy.panels[panel::Id::DockInspector] = placeholder(tr("가설 · 통과 기준"), this);
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
    if (it != m_docks.end()) {
        it->second->setVisible(visible);
    }
}

void Shell::addDock(panel::Id id, QStackedWidget *stack)
{
    QDockWidget *dock = makeDock(fromView(panel::idOf(id)), fromView(panel::info(id).label), *m_theme, stack, this);
    if (id == panel::Id::DockNavigator) {
        dock->widget()->setMinimumWidth(navigatorWidth);
    } else if (id == panel::Id::DockInspector) {
        dock->widget()->setMinimumWidth(inspectorWidth);
    }
    addDockWidget(areaFor(id), dock);
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
    for (const Workspace &each : m_workspaces) {
        m_actions->action(each.switchAction)->setChecked(each.id == id);
    }
    const auto context = id == workspace::Id::Copy ? workspace::Context::Copy : workspace::Context::Routine;
    m_actions->setActiveContext(context);
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

void Shell::showPreferences(int page)
{
    PreferencesDialog dialog(*m_theme, *m_settings, *m_audio, this);
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
    sync(action::Id::ViewDockBottom, panel::Id::DockTimeline);
}

void Shell::resetLayout()
{
    for (const Workspace &ws : m_workspaces) {
        m_settings->setLayoutState(ws.id, QByteArray());
    }
    for (auto &[id, dock] : m_docks) {
        removeDockWidget(dock);
        addDockWidget(areaFor(id), dock);
        dock->show();
    }
    switchTo(m_active);
}

void Shell::closeEvent(QCloseEvent *event)
{
    saveLayout();
    QMainWindow::closeEvent(event);
}

} // namespace cc::ui
