// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Shell smoke test on the offscreen platform: the window builds from the registry, docks carry their panel ids,
// workspaces switch, panel maximize / restore works, actions drive the session.
#include "generated/actionids.h"
#include "generated/panelids.h"
#include "ui/audio/audioservice.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/keymap.h"
#include "ui/shell/settings.h"
#include "ui/shell/shell.h"
#include "ui/theme/style.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QMenuBar>
#include <QTest>
#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

routine::RoutineState shellLoopState()
{
    routine::RoutineState state;
    state.moduleId = "02";
    state.modeId = "diatonic-loop";
    return state;
}

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "shell";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    Theme theme;
    ActionRegistry actions;
    Keymap keymap{userDir};
    RoutineSession session{routine::builtInRoutineDefinition(), shellLoopState()};

    Fixture()
    {
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(paths.theme(true)));
        QApplication::setStyle(new CopyCoachStyle(theme)); // NOLINT(cppcoreguidelines-owning-memory) app owns it
    }
};

} // namespace

TEST_CASE("shell: builds from the registry and the docks are named by panel id")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    QTest::qWait(50);

    REQUIRE(shell.menuBar()->actions().size() == 8);
    REQUIRE(shell.minimumHeight() == 660);
    const auto docks = shell.findChildren<QDockWidget *>();
    REQUIRE(docks.size() == 3); // navigator, inspector, timeline (copy-only docks come later)
    for (const QDockWidget *dock : docks) {
        bool known = false;
        for (const panel::Info &info : panel::infos) {
            known = known || dock->objectName() == fromView(info.id);
        }
        INFO(dock->objectName().toStdString());
        REQUIRE(known);
    }
    REQUIRE(f.actions.action(action::Id::WorkspaceRoutine)->isChecked());
    REQUIRE(f.actions.action(action::Id::RoutineChordNext)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::CopyPhaseLoop)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::FileNew)->isEnabled()); // not implemented yet: disabled, not hidden
}

TEST_CASE("shell: workspace switch, panel maximize and routine actions")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    QTest::qWait(50);

    f.actions.action(action::Id::WorkspaceCopy)->trigger();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Copy);
    REQUIRE(f.actions.action(action::Id::CopyPhaseLoop)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::RoutineChordNext)->isEnabled());
    f.actions.action(action::Id::WorkspaceRoutine)->trigger();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Routine);

    f.actions.action(action::Id::ViewPanelMaximize)->trigger();
    REQUIRE(shell.isPanelMaximized());
    for (const QDockWidget *dock : shell.findChildren<QDockWidget *>()) {
        REQUIRE(dock->isHidden());
    }
    f.actions.action(action::Id::ViewRestore)->trigger();
    REQUIRE_FALSE(shell.isPanelMaximized());

    REQUIRE(f.session.selectedSlot() == 0);
    f.actions.action(action::Id::RoutineChordNext)->trigger();
    REQUIRE(f.session.selectedSlot() == 1);
    f.actions.action(action::Id::RoutineChordSelectN, 5)->trigger();
    REQUIRE(f.session.selectedSlot() == 4);
    f.actions.action(action::Id::RoutineTempoUp5)->trigger();
    REQUIRE(f.session.state().tempoBpm == 65.0);
    f.actions.action(action::Id::RoutineModuleN, 3)->trigger();
    REQUIRE(f.session.state().moduleId == "03");
    f.actions.action(action::Id::FretboardLabel)->trigger();
    REQUIRE(f.settings.stringValue(setting::Key::ViewFretboardLabel) == QStringLiteral("name"));
    f.actions.action(action::Id::RoutineTimerToggle)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Running);
    REQUIRE(shell.timer()->currentModule() == 2); // module 03 entered above
    REQUIRE(shell.timer()->timer().lengthSeconds() == 600);
    shell.timer()->advance(600);
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Finished);
    f.actions.action(action::Id::RoutineTimerReset)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Idle);
    REQUIRE(shell.timer()->timer().elapsedSeconds(2) == 600);
    f.actions.action(action::Id::TransportPlayPause)->trigger();
    QTest::qWait(120);
    REQUIRE(audio.isPlaying());
    f.actions.action(action::Id::TransportPanic)->trigger();
    QTest::qWait(120);
    REQUIRE_FALSE(audio.isPlaying());
}

TEST_CASE("shell: renders dark and light screenshots into the build folder")
{
    // Not an assertion about pixels: the images under <build>/shots/ are for eyes (mockup M2 comparison).
    for (const bool dark : {true, false}) {
        Fixture f;
        REQUIRE(f.theme.load(f.paths.theme(dark)));
        QApplication::setPalette(f.theme.palette(Surface::Canvas));
        AudioService audio(f.paths, f.settings);
        Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
        f.session.setOption("upper", "on");
        shell.resize(1280, 720);
        shell.show();
        QTest::qWait(100);
        const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
        REQUIRE(shots.mkpath(QStringLiteral(".")));
        const QString file =
            shots.filePath(dark ? QStringLiteral("shell-dark.png") : QStringLiteral("shell-light.png"));
        REQUIRE(shell.grab().save(file));
    }
}
