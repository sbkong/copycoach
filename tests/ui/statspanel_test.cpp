// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Stats dock, routine context (stats.md sections 3, 7): empty state without a reader; with a seeded store the
// model carries today's minutes, the streak and the key coverage, and refreshes from the timer by the minute.
#include "core/routine/definition.h"
#include "core/routine/state.h"
#include "core/stats/sqlitestore.h"
#include "ui/routine/routinerecorder.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/routine/statsmodel.h"
#include "ui/routine/statspanel.h"
#include "ui/shell/apppaths.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QImage>
#include <QLabel>
#include <QTest>
#include <filesystem>
#include <string>

using namespace cc;
using namespace cc::ui;

namespace
{

routine::RoutineState loopState()
{
    routine::RoutineState state;
    state.moduleId = "02";
    state.modeId = "diatonic-loop";
    state.keys = {0, 5}; // C, F
    return state;
}

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "statspanel";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    Theme theme;
    RoutineSession session{routine::builtInRoutineDefinition(), loopState()};
    RoutineTimerController timer{settings, session};

    Fixture()
    {
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(paths.theme(true)));
    }
};

stats::StatsEvent start(const char *module, nlohmann::json keys)
{
    return {std::string(stats::event::routineModuleStart),
            {{"module", module}, {"mode", "diatonic-loop"}, {"keys", std::move(keys)}, {"tempo", 60}}};
}

stats::StatsEvent tick(const char *module, int elapsed)
{
    return {std::string(stats::event::routineTimerTick), {{"module", module}, {"elapsed", elapsed}}};
}

stats::StatsEvent stop(const char *module, int elapsed)
{
    return {std::string(stats::event::routineModuleStop), {{"module", module}, {"elapsed", elapsed}}};
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

/// Pixels of the accent colour inside row `module` of a grabbed bars view: the filled part of its bar.
int accentPixelsInRow(const QImage &image, int module, const QColor &accent)
{
    const int rowHeight = image.height() / StatsModel::moduleCount;
    int count = 0;
    for (int y = module * rowHeight; y < (module + 1) * rowHeight; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += image.pixelColor(x, y).rgb() == accent.rgb() ? 1 : 0;
        }
    }
    return count;
}

constexpr int pitchC = 0;
constexpr int pitchDb = 1;
constexpr int pitchF = 5;
constexpr int module01 = 0;
constexpr int module03 = 2;
constexpr int defaultDockWidth = 260; // the inspector minimum the stats tab shares (shell.cpp)

} // namespace

TEST_CASE("stats dock: without a reader the model is empty and the panel shows the empty state text")
{
    const Fixture f;
    const StatsModel model(nullptr, f.settings, f.timer);
    REQUIRE_FALSE(model.hasRecords());
    REQUIRE(model.todayMinutes() == 0);
    REQUIRE(model.streakDays() == 0);
    StatsPanel panel(f.theme, model, f.timer);
    panel.show();
    QTest::qWait(20);
    REQUIRE(panel.isEmptyStateShown());
    const QLabel *empty = labelNamed(&panel, QStringLiteral("기록 빈 상태"));
    REQUIRE(empty != nullptr);
    REQUIRE(empty->text() == QStringLiteral("아직 기록이 없습니다. 루틴 타이머를 시작하면 자동으로 쌓입니다."));
}

TEST_CASE("stats dock: at the default dock width the module bars stay visible, the label elides instead")
{
    const Fixture f;
    stats::SqliteStatsStore store;
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(start("03", {"C"}));
    store.record(stop("03", 150)); // a quarter of the default ten-minute target
    REQUIRE(f.timer.lengthSeconds(module03) == 600);
    const StatsModel model(&store, f.settings, f.timer);
    StatsPanel panel(f.theme, model, f.timer);
    panel.resize(defaultDockWidth, 400);
    panel.show();
    QTest::qWait(20);
    auto *bars = panel.findChild<ModuleBarsView *>();
    REQUIRE(bars != nullptr);
    REQUIRE(bars->width() <= defaultDockWidth);
    const QImage image = bars->grab().toImage();
    const QColor accent = f.theme.color(theme::Token::AccentColorDefault);
    REQUIRE(accentPixelsInRow(image, module03, accent) > 0);
    REQUIRE(accentPixelsInRow(image, module01, accent) == 0); // nothing practised in 01: an empty track
}

TEST_CASE("stats dock: a module id outside 01..04 is shown nowhere, not credited to module 01")
{
    const Fixture f;
    stats::SqliteStatsStore store;
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(stop("09", 600)); // written by another version of the app
    store.record(stop("02", 60));
    const StatsModel model(&store, f.settings, f.timer);
    REQUIRE(model.todaySeconds(module01) == 0);
    REQUIRE(model.todaySeconds(1) == 60);
    REQUIRE(model.todayMinutes() == 1);
}

TEST_CASE("stats dock: a seeded store gives today's minutes per module, the streak and the key coverage")
{
    Fixture f;
    stats::SqliteStatsStore store; // the system clock: everything is "today"
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(start("02", {"C", "F"}));
    store.record(tick("02", 60));
    store.record(tick("02", 360));
    store.record(stop("02", 360));
    store.record(start("03", {"Db"}));
    store.record(stop("03", 30));
    REQUIRE(store.endSession());
    REQUIRE(store.beginSession("routine", "0.1.0-test")); // the session the live recorder writes into

    const RoutineRecorder recorder(store, f.timer, f.session); // before the model, as in the shell
    const StatsModel model(&store, f.settings, f.timer);
    REQUIRE(model.hasRecords());
    REQUIRE(model.todaySeconds(1) == 360);
    REQUIRE(model.todaySeconds(2) == 30);
    REQUIRE(model.todayMinutes() == 6);
    REQUIRE(model.streakDays() == 1); // 6 min today >= stats.min-day-minutes (5)
    REQUIRE(model.coverage().at("02").at("C") == 1);
    StatsPanel panel(f.theme, model, f.timer);
    panel.show();
    QTest::qWait(20);
    REQUIRE_FALSE(panel.isEmptyStateShown());
    REQUIRE(labelNamed(&panel, QStringLiteral("연속 일수"))->text() == QStringLiteral("연속 1일"));
    REQUIRE(panel.heatmap()->level(1, pitchC) == 4); // every seeded cell is the busiest: full accent
    REQUIRE(panel.heatmap()->level(1, pitchF) == 4);
    REQUIRE(panel.heatmap()->level(2, pitchDb) == 4);
    REQUIRE(panel.heatmap()->level(0, pitchC) == 0);

    // the timer drives the refresh: start (state change) and the first full minute (shown minute change)
    f.timer.toggle();
    REQUIRE(model.coverage().at("02").at("C") == 2); // the recorder's start listed C and F
    REQUIRE(panel.heatmap()->level(1, pitchC) == 4);
    REQUIRE(panel.heatmap()->level(1, pitchF) == 4);
    REQUIRE(panel.heatmap()->level(2, pitchDb) == 2); // 1 of 2: half
    f.timer.advance(59);
    REQUIRE(model.todaySeconds(1) == 360); // no tick, no query
    f.timer.advance(1);
    REQUIRE(model.todaySeconds(1) == 420); // the new session's running total adds to the old one
    REQUIRE(model.todayMinutes() == 7);
    f.timer.toggle();

    f.settings.setValue(setting::Key::StatsMinDayMinutes, nlohmann::json(10)); // the streak follows the setting
    REQUIRE(model.streakDays() == 0);
    REQUIRE(labelNamed(&panel, QStringLiteral("연속 일수"))->text() == QStringLiteral("연속 0일"));
}
