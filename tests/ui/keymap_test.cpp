// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// keymap.json (naming.md section 5) and the shortcuts page (M8b): defaults, overrides, conflicts, apply, round trip.
#include "ui/prefs/shortcutspage.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/keymap.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <QTest>
#include <filesystem>
#include <fstream>

using namespace cc;
using namespace cc::ui;

namespace
{

std::filesystem::path freshDir(const char *name)
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "keymap" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("keymap: registry defaults, template instances, overrides and the saved file")
{
    const auto dir = freshDir("roundtrip");
    Keymap keymap(dir);
    REQUIRE(keymap.load()); // no file: defaults
    REQUIRE(Keymap::defaultKeys(QStringLiteral("transport.play-pause"))
            == QList<QKeySequence>{QKeySequence(Qt::Key_Space)});
    REQUIRE(Keymap::defaultKeys(QStringLiteral("routine.chord.select-4"))
            == QList<QKeySequence>{QKeySequence(Qt::Key_4)});
    REQUIRE(Keymap::defaultKeys(QStringLiteral("no.such")).isEmpty());
    REQUIRE(keymap.isDefault(QStringLiteral("transport.loop")));
    REQUIRE(contextOfActionId(QStringLiteral("routine.chord.select-4")) == workspace::Context::Routine);
    REQUIRE(contextOfActionId(QStringLiteral("editor.rest")) == workspace::Context::Editor);

    keymap.setKeys(QStringLiteral("transport.loop"), {QKeySequence(QStringLiteral("Ctrl+L"))});
    keymap.setKeys(QStringLiteral("file.new"),
                   Keymap::defaultKeys(QStringLiteral("file.new"))); // same as default: not stored
    REQUIRE(keymap.overrideCount() == 1);
    REQUIRE_FALSE(keymap.isDefault(QStringLiteral("transport.loop")));
    REQUIRE(keymap.save());

    Keymap reloaded(dir);
    REQUIRE(reloaded.load());
    REQUIRE(reloaded.keys(QStringLiteral("transport.loop"))
            == QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+L"))});
    REQUIRE(reloaded.keys(QStringLiteral("file.new")) == Keymap::defaultKeys(QStringLiteral("file.new")));
    std::ifstream in(dir / "keymap.json");
    const auto doc = nlohmann::json::parse(in);
    REQUIRE(doc["preset"] == "user");
    REQUIRE(doc["keys"].size() == 1);

    ActionRegistry registry;
    reloaded.apply(registry);
    REQUIRE(registry.action(action::Id::TransportLoop)->shortcut() == QKeySequence(QStringLiteral("Ctrl+L")));
    reloaded.resetAll();
    REQUIRE(reloaded.overrideCount() == 0);
}

TEST_CASE("keymap: an override saved under a renamed id loads under the new one and is saved there")
{
    const auto dir = freshDir("legacy");
    {
        std::ofstream out(dir / "keymap.json");
        out << R"({"schema_version": 1, "preset": "user", "keys": {"copy.phase.loop": ["Ctrl+Shift+4"], "copy.ab.cycle": ["Ctrl+Shift+B"]}})";
    }
    Keymap keymap(dir);
    REQUIRE(keymap.load());
    REQUIRE(keymap.keys(QStringLiteral("copy.stage.4"))
            == QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+Shift+4"))});
    REQUIRE(keymap.save());
    std::ifstream in(dir / "keymap.json");
    const auto doc = nlohmann::json::parse(in);
    REQUIRE(doc["keys"].contains("copy.stage.4"));
    REQUIRE_FALSE(doc["keys"].contains("copy.phase.loop"));
    REQUIRE_FALSE(doc["keys"].contains("copy.ab.cycle")); // retired (legacy without successor): dropped
}

TEST_CASE("keymap: conflicts within a context and against global keys")
{
    const Keymap keymap(freshDir("conflicts"));
    const auto same = keymap.conflicts(QStringLiteral("view.dock.stats"), QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    REQUIRE(same.size() == 1);
    REQUIRE(same.front().actionId == QStringLiteral("view.panel.maximize"));
    const auto routineVsGlobal = keymap.conflicts(QStringLiteral("routine.chord.next"), QKeySequence(Qt::Key_Space));
    REQUIRE(routineVsGlobal.size() == 1); // routine key would shadow the global play / pause
    const auto routineVsCopy =
        keymap.conflicts(QStringLiteral("routine.chord.next"), QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    REQUIRE(routineVsCopy.empty()); // different workspace contexts never meet
    REQUIRE(keymap.conflicts(QStringLiteral("routine.chord.next"), QKeySequence()).empty());
}

TEST_CASE("shortcuts page: tabs filter by context, assigning a free key saves, a taken key is refused inline")
{
    const auto dir = freshDir("page");
    Theme theme;
    REQUIRE(theme.load(std::filesystem::path(CC_DATA_DIR) / "ui" / "theme-dark.json"));
    ActionRegistry actions;
    Keymap keymap(dir);
    ShortcutsPage page(theme, actions, keymap);
    page.show();
    QTest::qWait(30);
    const KeymapModel *model = page.model();
    REQUIRE(model->rowCount() > 10); // global tab
    REQUIRE(model->columnCount() == 3);

    page.selectAction(QStringLiteral("routine.chord.next"));
    REQUIRE(page.selectedActionId() == QStringLiteral("routine.chord.next"));
    REQUIRE(model
                ->data(model->index(model->rowOf(QStringLiteral("routine.chord.next")), KeymapModel::keyColumn),
                       Qt::DisplayRole)
                .toString()
            == QKeySequence(Qt::Key_PageDown).toString(QKeySequence::NativeText));

    REQUIRE_FALSE(page.assignKey(QKeySequence(Qt::Key_PageUp))); // taken by routine.chord.prev
    REQUIRE(model
                ->data(model->index(model->rowOf(QStringLiteral("routine.chord.next")), KeymapModel::keyColumn),
                       KeymapModel::conflictRole)
                .toBool());
    REQUIRE(keymap.isDefault(QStringLiteral("routine.chord.next")));

    REQUIRE(page.assignKey(QKeySequence(QStringLiteral("Ctrl+Alt+J"))));
    REQUIRE_FALSE(keymap.isDefault(QStringLiteral("routine.chord.next")));
    REQUIRE(actions.action(action::Id::RoutineChordNext)->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+J")));
    REQUIRE(std::filesystem::exists(dir / "keymap.json"));
    REQUIRE(model
                ->data(model->index(model->rowOf(QStringLiteral("routine.chord.next")), KeymapModel::sourceColumn),
                       Qt::DisplayRole)
                .toString()
                .startsWith(QStringLiteral("사용자")));

    page.restoreDefaults();
    REQUIRE(keymap.overrideCount() == 0);
    REQUIRE(actions.action(action::Id::RoutineChordNext)->shortcut() == QKeySequence(Qt::Key_PageDown));
}
