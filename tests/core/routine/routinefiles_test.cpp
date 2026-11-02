// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routines/default.json and routine-state.json: loading, validation, atomic save (states.md section 5).
#include "core/io/jsonfile.h"
#include "core/routine/definition.h"
#include "core/routine/state.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::routine;

namespace
{

std::filesystem::path tempDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests";
    std::filesystem::create_directories(dir);
    return dir;
}

void writeText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("definition: data/routines/default.json loads and equals the built-in fallback")
{
    const auto loaded = loadRoutineDefinition(std::filesystem::path(CC_DATA_DIR) / "routines" / "default.json");
    REQUIRE(loaded.has_value());
    const RoutineDefinition builtIn = builtInRoutineDefinition();
    REQUIRE(loaded->modules.size() == builtIn.modules.size());
    for (std::size_t i = 0; i < builtIn.modules.size(); ++i) {
        INFO("module " << builtIn.modules[i].id);
        REQUIRE(loaded->modules[i].id == builtIn.modules[i].id);
        REQUIRE(loaded->modules[i].defaultMinutes == builtIn.modules[i].defaultMinutes);
        REQUIRE(loaded->modules[i].modes.size() == builtIn.modules[i].modes.size());
        for (std::size_t m = 0; m < builtIn.modules[i].modes.size(); ++m) {
            REQUIRE(loaded->modules[i].modes[m].id == builtIn.modules[i].modes[m].id);
            REQUIRE(loaded->modules[i].modes[m].options == builtIn.modules[i].modes[m].options);
        }
    }
    for (std::size_t day = 0; day < 7; ++day) {
        REQUIRE(loaded->weekdayPlan[day].mode == builtIn.weekdayPlan[day].mode);
        REQUIRE(loaded->weekdayPlan[day].keys == builtIn.weekdayPlan[day].keys);
    }
    // Module 03 open mode offers both open cycles (routine-spec.md section 4.5).
    const ModeDefinition &open = builtIn.modules[2].modes[1];
    REQUIRE(open.id == "open");
    REQUIRE(open.options.at("cycle") == std::vector<std::string>{"2-7", "3-6"});
}

TEST_CASE("definition: broken files are reported as values, not crashes")
{
    const auto dir = tempDir();
    REQUIRE(loadRoutineDefinition(dir / "missing.json").error().code == ErrorCode::FileNotFound);

    writeText(dir / "newer.json", R"({"schema_version": 99, "modules": []})");
    REQUIRE(loadRoutineDefinition(dir / "newer.json").error().code == ErrorCode::SchemaTooNew);

    writeText(dir / "badid.json", R"({"schema_version": 1, "modules": [{"id": "05", "modes": []}]})");
    const auto badId = loadRoutineDefinition(dir / "badid.json");
    REQUIRE(badId.error().code == ErrorCode::FileCorrupt);
    REQUIRE(badId.error().detail.contains("05"));

    writeText(dir / "badkeys.json", R"({"schema_version": 1, "modules": [{"id": "01", "modes": []}],
                                        "weekday_plan": {"mon": {"mode": 2, "keys": ["C", "H"]}}})");
    REQUIRE(loadRoutineDefinition(dir / "badkeys.json").error().code == ErrorCode::FileCorrupt);

    writeText(dir / "garbage.json", "{ not json");
    REQUIRE(loadRoutineDefinition(dir / "garbage.json").error().code == ErrorCode::FileCorrupt);

    writeText(dir / "custom.json", R"({"schema_version": 1, "modules": [{"id": "02", "modes": []}],
                                       "weekday_plan": {"tue": {"mode": 5, "keys": ["Eb", "Ab"]}}})");
    const auto custom = loadRoutineDefinition(dir / "custom.json");
    REQUIRE(custom.has_value());
    REQUIRE(custom->weekdayPlan[2].mode == 5);
    REQUIRE(custom->weekdayPlan[2].keys == std::array<theory::PitchClass, 2>{3, 8});
    REQUIRE(custom->weekdayPlan[1].mode == 2); // untouched day keeps the built-in plan
}

TEST_CASE("state: save is atomic with a backup and load round-trips every field")
{
    const auto path = tempDir() / "routine-state.json";
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".bak");
    RoutineState state;
    state.moduleId = "03";
    state.modeId = "closed";
    state.options = {{"strings", "234"}, {"cycle", "3-6"}};
    state.keys = {0, 5, 10};
    state.currentKey = 5;
    state.tempoBpm = 84.0;
    state.timeSig = model::TimeSignature{3, 4};
    state.guitarSound = "guitar-nylon";
    REQUIRE(saveRoutineState(path, state).has_value());
    REQUIRE_FALSE(std::filesystem::exists(path.string() + ".bak"));
    REQUIRE(saveRoutineState(path, state).has_value());
    REQUIRE(std::filesystem::exists(path.string() + ".bak"));
    REQUIRE_FALSE(std::filesystem::exists(path.string() + ".tmp"));

    const auto loaded = loadRoutineState(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->moduleId == "03");
    REQUIRE(loaded->modeId == "closed");
    REQUIRE(loaded->options == state.options);
    REQUIRE(loaded->keys == state.keys);
    REQUIRE(loaded->currentKey == 5);
    REQUIRE(loaded->tempoBpm == 84.0);
    REQUIRE(loaded->timeSig == model::TimeSignature{3, 4});
    REQUIRE(loaded->guitarSound == "guitar-nylon");
    REQUIRE(loaded->metronomeSound == "click");

    writeText(path, R"({"schema_version": 1, "tempo_bpm": 500})");
    REQUIRE(loadRoutineState(path).error().code == ErrorCode::FileCorrupt);
}
