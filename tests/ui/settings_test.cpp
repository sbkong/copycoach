// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// settings.json / layout.json round trip (naming.md section 5): defaults from the registry, only changed keys saved.
#include "ui/shell/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <filesystem>
#include <fstream>

using namespace cc;
using namespace cc::ui;

namespace
{

std::filesystem::path freshDir(const char *name)
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "settings" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("settings: defaults come from the registry and unchanged keys are not written")
{
    const auto dir = freshDir("defaults");
    Settings settings(dir);
    REQUIRE(settings.load()); // no files yet
    REQUIRE(settings.intValue(setting::Key::PracticeTempoDefault) == 60);
    REQUIRE(settings.stringValue(setting::Key::ViewFretboardLabel) == QStringLiteral("degree"));
    REQUIRE(settings.boolValue(setting::Key::PracticeAutoAssignWeekday));
    REQUIRE_FALSE(settings.boolValue(setting::Key::ViewKeyboardStrip));

    const QSignalSpy spy(&settings, &Settings::valueChanged);
    settings.setValue(setting::Key::PracticeTempoDefault, nlohmann::json(80));
    settings.setValue(setting::Key::PracticeTempoDefault, nlohmann::json(80)); // same value: no signal
    settings.setValue(setting::Key::ViewKeyboardStrip, nlohmann::json(true));
    REQUIRE(spy.count() == 2);
    REQUIRE(settings.save());

    const auto written = [&dir] {
        std::ifstream in(dir / "settings.json"); // closed before the next save: Windows cannot rename an open file
        return nlohmann::json::parse(in);
    };
    const auto doc = written();
    REQUIRE(doc["schema_version"] == 1);
    REQUIRE(doc["values"].size() == 2);
    REQUIRE(doc["values"]["practice.tempo-default"] == 80);

    settings.setValue(setting::Key::PracticeTempoDefault, nlohmann::json(60)); // back to default: key disappears
    REQUIRE(settings.save());
    REQUIRE(written()["values"].size() == 1);
}

TEST_CASE("settings: layout state and geometry survive a reload; a corrupt file is reported")
{
    const auto dir = freshDir("layout");
    {
        Settings settings(dir);
        settings.setWindowGeometry(QByteArrayLiteral("geom"));
        settings.setLayoutState(workspace::Id::Routine, QByteArrayLiteral("routine-state"));
        REQUIRE(settings.save());
    }
    Settings reloaded(dir);
    REQUIRE(reloaded.load());
    REQUIRE(reloaded.windowGeometry() == QByteArrayLiteral("geom"));
    REQUIRE(reloaded.layoutState(workspace::Id::Routine) == QByteArrayLiteral("routine-state"));
    REQUIRE(reloaded.layoutState(workspace::Id::Copy).isEmpty());

    std::ofstream(dir / "settings.json") << "{ not json";
    Settings corrupt(dir);
    const auto result = corrupt.load();
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == ErrorCode::FileCorrupt);
}
