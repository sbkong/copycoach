// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

// Practice records are events, not aggregates (stats.md section 4): whoever practises hands an event to the one
// IStatsSink it knows (section 5); every aggregate is a query over the stored events.
namespace cc::stats
{

struct StatsEvent {
    std::string type; // one of the names below; persistent once written (stats.md section 9)
    nlohmann::json payload = nlohmann::json::object();
};

/// Event type names (stats.md section 4; copy ones from copy-spec.md section 5, 2026-11-05).
namespace event
{
constexpr std::string_view routineModuleStart = "routine.module.start"; // {module, mode, keys[], tempo}
constexpr std::string_view routineModuleStop = "routine.module.stop"; // {module, mode, elapsed}
constexpr std::string_view routineTimerTick = "routine.timer.tick"; // {module, elapsed}: every minute while running
constexpr std::string_view copyProjectOpen = "copy.project.open"; // {project_id, stage} (stage since 2026-11-06)
constexpr std::string_view copyStageEnter = "copy.stage.enter"; // {project_id, stage 1..7}
constexpr std::string_view copyLoopPlay =
    "copy.loop.play"; // {project_id, range_id|null, bars|null, speed_pct, sources[]}
constexpr std::string_view copyLoopStop =
    "copy.loop.stop"; // {project_id, duration, repeats, range_id|null, bars|null, speed_pct}
constexpr std::string_view copyPredictShow = "copy.predict.show"; // {project_id, kind}
constexpr std::string_view copyPredictAccept = "copy.predict.accept"; // {project_id, kind, count}
constexpr std::string_view copyScoreImport = "copy.score.import"; // {project_id, format}
} // namespace event

class IStatsSink
{
public:
    virtual ~IStatsSink() = default;
    virtual void record(const StatsEvent &event) = 0;
};

/// Recording is off (setting stats.enabled) or the database could not be opened: events go nowhere.
class NullStatsSink final : public IStatsSink
{
public:
    void record(const StatsEvent & /*event*/) override
    {
    }
};

} // namespace cc::stats
