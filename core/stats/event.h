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

/// Event type names of the routine workspace (stats.md section 4). Copy events wait for the copy redesign.
namespace event
{
constexpr std::string_view routineModuleStart = "routine.module.start"; // {module, mode, keys[], tempo}
constexpr std::string_view routineModuleStop = "routine.module.stop"; // {module, mode, elapsed}
constexpr std::string_view routineTimerTick = "routine.timer.tick"; // {module, elapsed}: every minute while running
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
