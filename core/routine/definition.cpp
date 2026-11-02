// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/routine/definition.h"

#include "core/io/jsonfile.h"
#include "core/theory/pitch.h"
#include "core/theory/scaleform.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace cc::routine
{

namespace
{

using nlohmann::json;

constexpr std::array<std::string_view, 7> weekdayKeys{"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

std::unexpected<Error> corrupt(const std::filesystem::path &path, std::string detail)
{
    return fail(ErrorCode::FileCorrupt, path.string(), std::move(detail));
}

Result<std::vector<std::string>>
parseOptionValues(const json &values, const std::string &where, const std::filesystem::path &path)
{
    if (!values.is_array() || values.empty()) {
        return corrupt(path, where + " is not a non-empty array");
    }
    std::vector<std::string> out;
    out.reserve(values.size());
    for (const auto &v : values) {
        if (!v.is_string()) {
            return corrupt(path, where + " has a non-string value");
        }
        out.push_back(v.get<std::string>());
    }
    return out;
}

Result<ModeDefinition> parseMode(const json &node, const std::filesystem::path &path)
{
    if (!node.is_object() || !node.value("id", json()).is_string()) {
        return corrupt(path, "mode without string id");
    }
    ModeDefinition mode{node["id"], node.value("name", ""), {}};
    for (const auto &[name, values] : node.value("options", json::object()).items()) {
        auto parsed = parseOptionValues(values, "option " + name + " of mode " + mode.id, path);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        mode.options[name] = std::move(*parsed);
    }
    return mode;
}

Result<ModuleDefinition> parseModule(const json &node, const std::filesystem::path &path)
{
    const std::string id = node.value("id", "");
    if (id.size() != 2 || id < "01" || id > "04") {
        return corrupt(path, R"(module id must be "01".."04", got ")" + id + "\"");
    }
    ModuleDefinition module{id, node.value("name", ""), node.value("default_minutes", 10), {}};
    if (module.defaultMinutes <= 0) {
        return corrupt(path, "module " + id + " default_minutes must be positive");
    }
    for (const auto &modeNode : node.value("modes", json::array())) {
        auto mode = parseMode(modeNode, path);
        if (!mode) {
            return std::unexpected(mode.error());
        }
        module.modes.push_back(std::move(*mode));
    }
    return module;
}

Result<theory::WeekdayPlan> parseWeekday(const json &node, theory::Weekday day, const std::filesystem::path &path)
{
    theory::WeekdayPlan plan = theory::defaultWeekdayPlan(day);
    if (!node.is_object()) {
        return corrupt(path, "weekday_plan entry is not an object");
    }
    const int mode = node.value("mode", plan.mode);
    if (mode < 1 || mode > theory::modeCount) {
        return corrupt(path, "weekday mode out of 1..7");
    }
    plan.mode = mode;
    const json keys = node.value("keys", json("auto"));
    if (keys.is_string() && keys.get<std::string>() == "auto") {
        return plan;
    }
    if (!keys.is_array() || keys.size() != 2) {
        return corrupt(path, "weekday keys must be \"auto\" or two pitch names");
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const auto pitch = keys[i].is_string() ? theory::parsePitchClass(keys[i].get<std::string>()) : std::nullopt;
        if (!pitch) {
            return corrupt(path, "weekday key is not a pitch name");
        }
        plan.keys[i] = pitch.value_or(0);
    }
    plan.review = false;
    return plan;
}

} // namespace

Result<RoutineDefinition> loadRoutineDefinition(const std::filesystem::path &path)
{
    auto document = io::readJsonFile(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    const auto version = io::schemaVersionOf(*document, routineSchemaVersion, path);
    if (!version) {
        return std::unexpected(version.error());
    }
    RoutineDefinition definition;
    definition.schemaVersion = *version;
    const json modules = document->value("modules", json());
    if (!modules.is_array() || modules.empty()) {
        return corrupt(path, "modules missing");
    }
    for (const auto &node : modules) {
        auto module = parseModule(node, path);
        if (!module) {
            return std::unexpected(module.error());
        }
        definition.modules.push_back(std::move(*module));
    }
    const json plan = document->value("weekday_plan", json::object());
    for (std::size_t day = 0; day < weekdayKeys.size(); ++day) {
        const auto weekday = static_cast<theory::Weekday>(day);
        auto parsed = parseWeekday(plan.value(std::string(weekdayKeys[day]), json::object()), weekday, path);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        definition.weekdayPlan[day] = *parsed;
    }
    return definition;
}

RoutineDefinition builtInRoutineDefinition()
{
    RoutineDefinition definition;
    definition.modules = {
        ModuleDefinition{"01",
                         "스케일",
                         10,
                         {ModeDefinition{"diagonal",
                                         "대각선 메이저 스케일",
                                         {{"root_on", {"5A", "6E"}}, {"mode", {"1", "2", "3", "4", "5", "6", "7"}}}}}},
        ModuleDefinition{"02",
                         "쉘 + 드랍 2",
                         10,
                         {ModeDefinition{"diatonic-loop",
                                         "Diatonic Loop",
                                         {{"order", {"circle5", "scale"}}, {"upper", {"off", "on"}}}},
                          ModeDefinition{"secondary-dominant",
                                         "Secondary Dominant",
                                         {{"upper", {"off", "diatonic", "chromatic"}}}}}},
        ModuleDefinition{"03",
                         "트라이어드",
                         10,
                         {ModeDefinition{"closed",
                                         "Closed Voicing",
                                         {{"strings", {"123", "234", "345", "456"}},
                                          {"inversion", {"auto", "root", "1st", "2nd"}},
                                          {"cycle", {"2-7", "3-6", "4-5"}},
                                          {"direction", {"up", "down"}}}},
                          ModeDefinition{"open", "Open Voicing", {{"cycle", {"2-7"}}, {"direction", {"up", "down"}}}}}},
        ModuleDefinition{"04", "라인 (보류)", 30, {}},
    };
    for (std::size_t day = 0; day < definition.weekdayPlan.size(); ++day) {
        definition.weekdayPlan[day] = theory::defaultWeekdayPlan(static_cast<theory::Weekday>(day));
    }
    return definition;
}

} // namespace cc::routine
