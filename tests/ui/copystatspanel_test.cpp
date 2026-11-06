// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Stats dock, copy context (stats.md section 3): the song's time today and in all, one table row per saved
// range in project order plus the outside-a-range row, re-read when playback stops. The store is in memory.
#include "core/edit/commands.h"
#include "core/model/scorebuilder.h"
#include "core/project/project.h"
#include "core/stats/event.h"
#include "core/stats/sqlitestore.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/copystatsmodel.h"
#include "ui/copy/copystatspanel.h"
#include "ui/copy/projectsession.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/apppaths.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QTest>
#include <QTreeWidget>

#include <chrono>
#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "copystats";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    Theme theme;
    AudioService audio{paths, settings};
    ScoreEditor editor;
    ProjectSession project{editor, settings};
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now(); // the store's clock, movable
    stats::SqliteStatsStore store{[this] {
        return now;
    }};

    Fixture()
    {
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(paths.theme(true)));
        REQUIRE(store.open(":memory:"));
        REQUIRE(store.beginSession("routine", "0.1.0-test"));
    }

    void loopStop(const nlohmann::json &range, int duration, int repeats, int speed)
    {
        store.record({std::string(stats::event::copyLoopStop),
                      {{"project_id", project.project().projectId},
                       {"range_id", range},
                       {"bars", nullptr},
                       {"duration", duration},
                       {"repeats", repeats},
                       {"speed_pct", speed}}});
    }
};

} // namespace

TEST_CASE("copystats: time text")
{
    CHECK(practiceTimeText(40) == QStringLiteral("40초"));
    CHECK(practiceTimeText(300) == QStringLiteral("5분"));
    CHECK(practiceTimeText(3900) == QStringLiteral("1시간 5분"));
}

TEST_CASE("copystats: empty without a project, then one row per range once loops were played")
{
    Fixture f;
    CopyStatsModel model(&f.store, f.project, f.audio);
    CopyStatsPanel panel(f.theme, model);
    panel.resize(360, 300);
    panel.show();
    QTest::qWait(20);
    REQUIRE_FALSE(model.hasProject());
    auto *empty = panel.findChild<QLabel *>();
    REQUIRE(empty != nullptr);
    REQUIRE(empty->isVisible());
    REQUIRE(empty->text().contains(QStringLiteral("프로젝트")));

    REQUIRE(f.project.create(f.userDir / "song"));
    REQUIRE(model.hasProject());
    REQUIRE_FALSE(model.hasRecords());
    REQUIRE(empty->text().contains(QStringLiteral("원곡을 재생")));

    model::IdGenerator ids = model::IdGenerator::seeded(3);
    model::ScoreSpec spec;
    spec.measures.assign(4, model::MeasureSpec{{{model::Dur::Whole, {{1, 3}}}}});
    f.project.importScore(model::buildScore(spec, ids)); // ranges need bars to point at
    const project::Range a{ids.next(), "A 섹션", 0, 3, 0, 100, 0};
    REQUIRE(f.editor.runCommand(edit::setRange(f.project.project(), a)));
    f.loopStop(a.id, 90, 3, 70);
    f.loopStop(a.id, 30, 1, 85);
    f.loopStop(nullptr, 100, 0, 100);
    f.store.record(
        {std::string(stats::event::copyStageEnter), {{"project_id", f.project.project().projectId}, {"stage", 4}}});
    f.now += std::chrono::minutes(2); // the open session ends "now": stage 4 has run two minutes
    model.refresh(); // what the engine's stop does
    REQUIRE(model.hasRecords());
    REQUIRE(model.todaySeconds() == 220);
    REQUIRE(model.totalSeconds() == 220);
    REQUIRE_FALSE(model.stageSeconds().empty()); // the session is open: stage 4 counts up to now
    REQUIRE(model.stageSeconds().contains(4));
    REQUIRE(model.rows().size() == 2);
    REQUIRE(model.rows()[0].name == QStringLiteral("A 섹션"));
    REQUIRE(model.rows()[0].stats.plays == 2);
    REQUIRE(model.rows()[0].stats.maxSpeedPct == 85);
    REQUIRE(model.rows()[1].rangeId.empty());

    const QTreeWidget *table = panel.table();
    REQUIRE(table->isVisible());
    REQUIRE(table->topLevelItemCount() == 2);
    CHECK(table->topLevelItem(0)->text(0) == QStringLiteral("A 섹션"));
    CHECK(table->topLevelItem(0)->text(1) == QStringLiteral("2"));
    CHECK(table->topLevelItem(0)->text(2) == QStringLiteral("2분"));
    CHECK(table->topLevelItem(0)->text(3) == QStringLiteral("4"));
    CHECK(table->topLevelItem(0)->text(4) == QStringLiteral("85 %"));
    CHECK(table->topLevelItem(1)->text(0) == QStringLiteral("구간 밖 재생"));

    QLabel *stages = nullptr;
    for (QLabel *label : panel.findChildren<QLabel *>()) {
        if (label->accessibleName() == QStringLiteral("단계별 시간")) {
            stages = label;
        }
    }
    REQUIRE(stages != nullptr);
    CHECK(stages->text().contains(QStringLiteral("4 프레이즈")));

    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(panel.grab().save(shots.filePath(QStringLiteral("copy-stats.png"))));

    f.project.close();
    REQUIRE_FALSE(model.hasProject());
    REQUIRE(empty->isVisible());
}
