// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Shell smoke test on the offscreen platform: the window builds from the registry, docks carry their panel ids,
// workspaces switch, panel maximize / restore works, actions drive the session.
#include "core/stats/event.h"
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
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QTest>
#include <QUrl>
#include <filesystem>
#include <vector>

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

struct CollectingSink final : stats::IStatsSink {
    std::vector<stats::StatsEvent> events;

    void record(const stats::StatsEvent &event) override
    {
        events.push_back(event);
    }
};

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
    REQUIRE(docks.size() == 5); // navigator, inspector, timeline, stats, audio track // navigator, inspector, timeline,
                                // stats (copy-only docks come later)
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
    REQUIRE_FALSE(f.actions.action(action::Id::FilePrint)->isEnabled()); // not implemented yet: disabled, not hidden
    REQUIRE(f.actions.action(action::Id::FileNew)->isEnabled());
    REQUIRE(f.actions.action(action::Id::EditUndo)->isEnabled());
    // wired actions must not sit in the not-yet-implemented list (piece 9 and 10 regressions)
    REQUIRE(f.actions.action(action::Id::AppCommandPalette)->isEnabled());
    REQUIRE(f.actions.action(action::Id::AppKeymapEditor)->isEnabled());
    REQUIRE(f.actions.action(action::Id::AppPreferences)->isEnabled());
    REQUIRE(f.actions.action(action::Id::ViewDockStats)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::ViewStatsFull)->isEnabled());
    // no reader: the status bar still shows today's minutes, as zero (stats.md section 3)
    QLabel *today = nullptr;
    for (QLabel *label : shell.findChildren<QLabel *>()) {
        if (label->accessibleName() == QStringLiteral("오늘 연습 시간")) {
            today = label;
        }
    }
    REQUIRE(today != nullptr);
    REQUIRE(today->text() == QStringLiteral("오늘 0분"));
}

TEST_CASE("shell: closing while the timer runs pauses it first, so the stop event carries the seconds")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    CollectingSink sink;
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap, &sink});
    shell.show();
    QTest::qWait(50);
    f.actions.action(action::Id::RoutineTimerToggle)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Running);
    shell.timer()->advance(70);
    REQUIRE(sink.events.size() == 2); // the start and the first minute's tick
    REQUIRE(shell.close());
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Paused);
    REQUIRE(sink.events.size() == 3);
    REQUIRE(sink.events.back().type == stats::event::routineModuleStop);
    REQUIRE(sink.events.back().payload["elapsed"] == 70);
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

TEST_CASE("shell: a MusicXML file dropped on the window is imported into the copy workspace")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Routine);
    const QString fixture = QString::fromStdWString(
        (std::filesystem::path(CC_FIXTURE_DIR) / "score" / "import-features.musicxml").wstring());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(fixture)});
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &enter);
    REQUIRE(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &drop);
    REQUIRE(drop.isAccepted());
    REQUIRE(shell.activeWorkspace() == workspace::Id::Copy);
    REQUIRE(shell.windowTitle().contains(QStringLiteral("저장되지 않은 프로젝트")));

    QMimeData other;
    other.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/nope.txt"))});
    QDragEnterEvent refused(QPointF(10, 10), Qt::CopyAction, &other, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &refused);
    REQUIRE_FALSE(refused.isAccepted());
}
