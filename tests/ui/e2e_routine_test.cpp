// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// End-to-end routine run on the offscreen platform: the real shell driven like a user through module 02, the
// module 03 open 3-6 chain and the closed start octave rule (routine-spec.md section 4.3), a timed practice that
// lands in a real SqliteStatsStore and comes back through the stats dock and the status bar (stats.md sections
// 3, 7, 9), the stats preferences page, and the dock action. One PNG per step lands in <build>/shots/e2e-NN-*.png
// for eyes; the assertions are about state.
#include "core/stats/sqlitestore.h"
#include "generated/actionids.h"
#include "generated/panelids.h"
#include "generated/settingkeys.h"
#include "ui/audio/audioservice.h"
#include "ui/fretboard/fretboardview.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/routine/chordstrip.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/routine/statsmodel.h"
#include "ui/routine/statspanel.h"
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
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTest>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace cc;
using namespace cc::ui;

namespace
{

constexpr int pitchC = 0;
constexpr int pitchF = 5;
constexpr int pitchG = 7;
constexpr int module03 = 2;
constexpr int secondsPerMinute = 60;
constexpr int minDayDefaultMinutes = 5;

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

/// The clock the store stamps events with: real "now" so that everything lands on today, advanced by the test
/// alongside the timer controller.
struct FakeClock {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

    [[nodiscard]] stats::Clock fn()
    {
        return [this] {
            return now;
        };
    }
};

routine::RoutineState startState()
{
    routine::RoutineState state;
    state.moduleId = "02";
    state.modeId = "diatonic-loop";
    state.keys = {pitchC};
    state.currentKey = pitchC;
    return state;
}

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "e2e";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    Theme theme;
    ActionRegistry actions;
    Keymap keymap{userDir};
    RoutineSession session{routine::builtInRoutineDefinition(), startState()};
    FakeClock clock;
    stats::SqliteStatsStore store{clock.fn()}; // declared before the shell: the recorder writes into it
    AudioService audio{paths, settings};

    Fixture()
    {
        std::filesystem::remove_all(userDir); // a fresh user folder: no settings, keymap or layout from last time
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(paths.theme(true)));
        QApplication::setStyle(new CopyCoachStyle(theme)); // NOLINT(cppcoreguidelines-owning-memory) app owns it
        QApplication::setPalette(theme.palette(Surface::Canvas));
        REQUIRE(store.open(":memory:"));
        REQUIRE(store.beginSession("routine", "e2e"));
    }

    [[nodiscard]] ShellContext context()
    {
        return ShellContext{&paths, &theme, &settings, &actions, &audio, &session, &keymap, &store, &store};
    }
};

void shoot(QWidget *widget, const QString &name)
{
    QTest::qWait(50);
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(widget->grab().save(shots.filePath(name)));
}

QLabel *labelNamed(const QWidget *root, const QString &accessibleName)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->accessibleName() == accessibleName) {
            return label;
        }
    }
    return nullptr;
}

QSpinBox *spinNamed(const QWidget *root, const QString &accessibleName)
{
    for (QSpinBox *spin : root->findChildren<QSpinBox *>()) {
        if (spin->accessibleName() == accessibleName) {
            return spin;
        }
    }
    return nullptr;
}

QPushButton *buttonTitled(const QWidget *root, const QString &text)
{
    for (QPushButton *button : root->findChildren<QPushButton *>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QDockWidget *dockOf(const Shell &shell, panel::Id id)
{
    for (QDockWidget *dock : shell.findChildren<QDockWidget *>()) {
        if (dock->objectName() == fromView(panel::idOf(id))) {
            return dock;
        }
    }
    return nullptr;
}

/// "x x 5 x 5 8": strings 6..1 as routine-spec.md section 4.3 writes a chord.
QString tabOf(const Slot &slot)
{
    QStringList cells;
    for (int string = 6; string >= 1; --string) {
        QString cell = QStringLiteral("x");
        for (const SlotNote &note : slot.notes) {
            if (note.position.string == string) {
                cell = QString::number(note.position.fret);
            }
        }
        cells << cell;
    }
    return cells.join(QLatin1Char(' '));
}

// Spec section 4.3, open 3-6 in C going up: C Am F Dm B° G Em C (chordName spells the diminished as "dim").
constexpr std::array<const char *, 8> openUpNames{"C", "Am", "F", "Dm", "Bdim", "G", "Em", "C"};
constexpr std::array<const char *, 8> openUpTabs{"x x 5 x 5 8",
                                                 "x x 7 x 5 8",
                                                 "x x 7 x 6 8",
                                                 "x x 7 10 x 10",
                                                 "x x 9 10 x 10",
                                                 "x x 9 x 8 10",
                                                 "x x 9 12 x 12",
                                                 "x x 5 x 5 8"};

void requireOpenChain(const std::vector<Slot> &chain, bool reversed)
{
    REQUIRE(chain.size() == 8);
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const std::size_t expected = reversed ? 7 - i : i;
        INFO("slot " << i + 1 << " " << chain[i].name.toStdString() << " " << tabOf(chain[i]).toStdString());
        REQUIRE(chain[i].name == QLatin1String(openUpNames[expected]));
        REQUIRE(tabOf(chain[i]) == QLatin1String(openUpTabs[expected]));
    }
}

/// Step 1: module 02, the chord strip has eight chips, the stats dock sits under the inspector and is empty.
void step1Module02(Fixture &f, Shell &shell)
{
    f.session.setModule("02");
    f.session.setMode("diatonic-loop");
    REQUIRE(shell.activeWorkspace() == workspace::Id::Routine);
    REQUIRE(f.session.slotList().size() == 8);
    REQUIRE(shell.findChildren<ChordChip *>().size() == 8);
    QDockWidget *dock = dockOf(shell, panel::Id::DockStats);
    REQUIRE(dock != nullptr);
    REQUIRE_FALSE(dock->isHidden());
    QDockWidget *inspector = dockOf(shell, panel::Id::DockInspector);
    REQUIRE(inspector != nullptr);
    REQUIRE(shell.tabifiedDockWidgets(inspector).contains(dock)); // stats.md section 3: a tab under the inspector
    auto *panel = shell.findChild<StatsPanel *>();
    REQUIRE(panel != nullptr);
    REQUIRE(panel->isEmptyStateShown());
    const QLabel *empty = labelNamed(panel, QStringLiteral("기록 빈 상태"));
    REQUIRE(empty != nullptr);
    REQUIRE(empty->text() == QStringLiteral("아직 기록이 없습니다. 루틴 타이머를 시작하면 자동으로 쌓입니다."));
    const QLabel *today = labelNamed(&shell, QStringLiteral("오늘 연습 시간"));
    REQUIRE(today != nullptr);
    REQUIRE(today->text() == QStringLiteral("오늘 0분"));
    shoot(&shell, QStringLiteral("e2e-01-routine-module02.png"));
    dock->raise();
    shoot(&shell, QStringLiteral("e2e-02-stats-dock-empty.png"));
}

/// Step 2: module 03 open 3-6 in C equals the spec chain both ways; the fretboard shows the re-fingered slots.
void step2OpenThirds(Fixture &f, Shell &shell)
{
    f.session.setModule("03");
    f.session.setMode("open");
    f.session.setOption("cycle", "3-6");
    f.session.setOption("direction", "up");
    f.session.setCurrentKey(pitchC);
    requireOpenChain(f.session.slotList(), false);
    auto *fretboard = shell.findChild<FretboardView *>();
    REQUIRE(fretboard != nullptr);
    f.session.setSelectedSlot(3); // Dm: the first chord whose middle voice moves to string 3
    REQUIRE(f.session.selectedSlot() == 3);
    shoot(fretboard, QStringLiteral("e2e-03-open36-slot4-dm.png"));
    f.session.setSelectedSlot(6); // Em
    shoot(fretboard, QStringLiteral("e2e-04-open36-slot7-em.png"));
    f.session.setOption("direction", "down");
    requireOpenChain(f.session.slotList(), true); // = the source's Cycle 3 card order
    f.session.setSelectedSlot(0);
    shoot(&shell, QStringLiteral("e2e-05-open36-down.png"));
}

/// Step 3: closed 1-2-3 auto: G starts an octave up (12 12 10, the closedStart rule), C at 5 5 3.
void step3ClosedStart(Fixture &f, Shell &shell)
{
    f.session.setMode("closed");
    f.session.setOption("strings", "123");
    f.session.setOption("inversion", "auto");
    f.session.setOption("cycle", "2-7");
    f.session.setOption("direction", "up");
    f.session.setCurrentKey(pitchG);
    REQUIRE(f.session.slotList().size() == 8);
    REQUIRE(f.session.slotList()[0].name == QStringLiteral("G"));
    REQUIRE(tabOf(f.session.slotList()[0]) == QStringLiteral("x x x 12 12 10"));
    shoot(&shell, QStringLiteral("e2e-06-closed-g-start.png"));
    f.session.setCurrentKey(pitchC);
    REQUIRE(f.session.slotList()[0].name == QStringLiteral("C"));
    REQUIRE(tabOf(f.session.slotList()[0]) == QStringLiteral("x x x 5 5 3"));
}

/// Moves the store's clock and the timer together, as the one-second clock would.
void practise(Fixture &f, const Shell &shell, int seconds)
{
    f.clock.now += std::chrono::seconds(seconds);
    shell.timer()->advance(seconds);
}

/// Step 4a: 150 s in module 03 reach the dock and the status bar through the real store.
void step4Timed(Fixture &f, Shell &shell)
{
    f.session.setKeys({pitchC});
    auto *model = shell.findChild<StatsModel *>();
    auto *panel = shell.findChild<StatsPanel *>();
    REQUIRE(model != nullptr);
    REQUIRE(panel != nullptr);
    f.actions.action(action::Id::RoutineTimerToggle)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Running);
    REQUIRE(shell.timer()->currentModule() == module03);
    REQUIRE_FALSE(panel->isEmptyStateShown()); // the start event listed a key: records exist
    practise(f, shell, secondsPerMinute);
    practise(f, shell, secondsPerMinute);
    practise(f, shell, secondsPerMinute / 2);
    REQUIRE(f.store.lastError().empty());
    // While running the dock follows the full-minute ticks: the last stored running total is 120 s.
    REQUIRE(model->todaySeconds(module03) == 2 * secondsPerMinute);
    f.actions.action(action::Id::RoutineTimerToggle)->trigger(); // pause: the stop event carries 150 s
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Paused);
    REQUIRE(model->todaySeconds(module03) == 150);
    REQUIRE(model->todayMinutes() == 2); // whole minutes, rounded down
    REQUIRE(labelNamed(&shell, QStringLiteral("오늘 연습 시간"))->text() == QStringLiteral("오늘 2분"));
    REQUIRE(f.settings.intValue(setting::Key::StatsMinDayMinutes) == minDayDefaultMinutes);
    REQUIRE(model->streakDays() == 0); // 150 s is under the five-minute minimum (stats.md section 4)
    REQUIRE(labelNamed(panel, QStringLiteral("연속 일수"))->text() == QStringLiteral("연속 0일"));
}

/// Step 4b: past five minutes the streak is one day; a second key gives the heatmap two cells.
void step4StreakAndKeys(Fixture &f, Shell &shell)
{
    const auto *model = shell.findChild<StatsModel *>();
    const auto *panel = shell.findChild<StatsPanel *>();
    f.actions.action(action::Id::RoutineTimerToggle)->trigger(); // resume
    practise(f, shell, 150); // 300 s: the tick at minute 5 refreshes the model
    REQUIRE(model->todaySeconds(module03) == minDayDefaultMinutes * secondsPerMinute);
    REQUIRE(model->todayMinutes() == minDayDefaultMinutes);
    REQUIRE(model->streakDays() == 1);
    REQUIRE(labelNamed(panel, QStringLiteral("연속 일수"))->text() == QStringLiteral("연속 1일"));
    f.actions.action(action::Id::RoutineTimerToggle)->trigger(); // pause
    f.session.setKeys({pitchF}); // the current key follows
    REQUIRE(f.session.state().currentKey == pitchF);
    f.actions.action(action::Id::RoutineTimerToggle)->trigger(); // start again: module.start lists F
    REQUIRE(model->coverage().at("03").at("C") == 2); // the start and the resume both listed C
    REQUIRE(model->coverage().at("03").at("F") == 1);
    REQUIRE(panel->heatmap()->level(module03, pitchC) > 0);
    REQUIRE(panel->heatmap()->level(module03, pitchF) > 0);
    REQUIRE(panel->heatmap()->level(module03, pitchG) == 0);
    f.actions.action(action::Id::RoutineTimerToggle)->trigger(); // pause
    QDockWidget *dock = dockOf(shell, panel::Id::DockStats);
    dock->raise();
    shoot(dock, QStringLiteral("e2e-07-stats-dock.png"));
    shoot(&shell, QStringLiteral("e2e-08-shell-after-practice.png"));
}

/// Step 5: preferences > 기록: the minimum changes the streak without a restart; restore brings 5 back.
void step5Preferences(Fixture &f, Shell &shell)
{
    const auto *model = shell.findChild<StatsModel *>();
    const auto *panel = shell.findChild<StatsPanel *>();
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &f.audio, &f.actions, &f.keymap, &f.paths},
                             &shell);
    dialog.show();
    dialog.showPage(PreferencesPage::Stats);
    REQUIRE(dialog.currentPage() == PreferencesPage::Stats);
    shoot(&dialog, QStringLiteral("e2e-09-prefs-stats.png"));
    QSpinBox *minDay = spinNamed(&dialog, QStringLiteral("하루 인정 최소 분"));
    REQUIRE(minDay != nullptr);
    REQUIRE(minDay->value() == minDayDefaultMinutes);
    minDay->setValue(2 * minDayDefaultMinutes); // ten: today's five minutes no longer count
    REQUIRE(f.settings.intValue(setting::Key::StatsMinDayMinutes) == 2 * minDayDefaultMinutes);
    REQUIRE(model->streakDays() == 0);
    REQUIRE(labelNamed(panel, QStringLiteral("연속 일수"))->text() == QStringLiteral("연속 0일"));
    minDay->setValue(1);
    REQUIRE(f.settings.intValue(setting::Key::StatsMinDayMinutes) == 1);
    REQUIRE(model->streakDays() == 1);
    REQUIRE(labelNamed(panel, QStringLiteral("연속 일수"))->text() == QStringLiteral("연속 1일"));
    QPushButton *restore = buttonTitled(&dialog, QStringLiteral("기본값 복원"));
    REQUIRE(restore != nullptr);
    restore->click();
    REQUIRE(f.settings.intValue(setting::Key::StatsMinDayMinutes) == minDayDefaultMinutes);
    REQUIRE(minDay->value() == minDayDefaultMinutes);
    REQUIRE(model->streakDays() == 1);
}

/// Step 6: view.dock.stats toggles the dock; the dock stays away in the copy workspace and returns in routine.
void step6DockAction(const Fixture &f, Shell &shell)
{
    QAction *toggle = f.actions.action(action::Id::ViewDockStats);
    const QDockWidget *dock = dockOf(shell, panel::Id::DockStats);
    REQUIRE(toggle->isEnabled());
    REQUIRE(toggle->isCheckable());
    REQUIRE(toggle->isChecked());
    REQUIRE_FALSE(dock->isHidden());
    toggle->trigger();
    REQUIRE(dock->isHidden());
    REQUIRE_FALSE(toggle->isChecked());
    toggle->trigger();
    REQUIRE_FALSE(dock->isHidden());
    REQUIRE(toggle->isChecked());
    f.actions.action(action::Id::WorkspaceCopy)->trigger();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Copy);
    REQUIRE(dock->isHidden());
    REQUIRE_FALSE(toggle->isChecked());
    shoot(&shell, QStringLiteral("e2e-10-copy-workspace.png"));
    f.actions.action(action::Id::WorkspaceRoutine)->trigger();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Routine);
    REQUIRE_FALSE(dock->isHidden());
    REQUIRE(toggle->isChecked());
}

} // namespace

TEST_CASE("e2e: a routine run through the real shell lands in the store, the dock, the status bar and the shots")
{
    Fixture f;
    Shell shell(f.context());
    shell.resize(1280, 720);
    shell.show();
    QTest::qWait(100);
    step1Module02(f, shell);
    step2OpenThirds(f, shell);
    step3ClosedStart(f, shell);
    step4Timed(f, shell);
    step4StreakAndKeys(f, shell);
    step5Preferences(f, shell);
    step6DockAction(f, shell);
}
