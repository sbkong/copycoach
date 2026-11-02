// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Routine timer -> practice events (routine-spec.md section 1 "기록", stats.md section 4).
#include "core/routine/definition.h"
#include "core/routine/state.h"
#include "ui/routine/routinerecorder.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/shell/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace cc;
using namespace cc::ui;

namespace
{

struct CollectingSink final : stats::IStatsSink {
    std::vector<stats::StatsEvent> events;

    void record(const stats::StatsEvent &event) override
    {
        events.push_back(event);
    }
};

} // namespace

TEST_CASE("routine recorder: start, a tick per full minute, stop with the running total; a module switch stops")
{
    routine::RoutineState state;
    state.moduleId = "02";
    state.modeId = "diatonic-loop";
    state.keys = {0, 5};
    RoutineSession session(routine::builtInRoutineDefinition(), state);
    Settings settings{std::filesystem::temp_directory_path() / "copycoach-tests" / "recorder"};
    RoutineTimerController timer(settings, session);
    CollectingSink sink;
    const RoutineRecorder recorder(sink, timer, session);

    timer.toggle();
    REQUIRE(sink.events.size() == 1);
    REQUIRE(sink.events[0].type == stats::event::routineModuleStart);
    REQUIRE(sink.events[0].payload["module"] == "02");
    REQUIRE(sink.events[0].payload["mode"] == "diatonic-loop");
    REQUIRE(sink.events[0].payload["keys"] == nlohmann::json::array({"C", "F"}));
    REQUIRE(sink.events[0].payload["tempo"] == 60.0);

    timer.advance(59);
    REQUIRE(sink.events.size() == 1); // no full minute yet
    timer.advance(1);
    REQUIRE(sink.events.size() == 2);
    REQUIRE(sink.events[1].type == stats::event::routineTimerTick);
    REQUIRE(sink.events[1].payload["elapsed"] == 60);
    timer.advance(90); // 150 s: one tick, carrying the running total
    REQUIRE(sink.events.size() == 3);
    REQUIRE(sink.events[2].payload["elapsed"] == 150);

    timer.toggle(); // pause
    REQUIRE(sink.events.size() == 4);
    REQUIRE(sink.events[3].type == stats::event::routineModuleStop);
    REQUIRE(sink.events[3].payload["module"] == "02");
    REQUIRE(sink.events[3].payload["elapsed"] == 150);

    timer.toggle(); // resume: a new start, ticks continue from the running total
    REQUIRE(sink.events.size() == 5);
    timer.advance(30); // 180 s
    REQUIRE(sink.events.size() == 6);
    REQUIRE(sink.events[5].payload["elapsed"] == 180);

    session.setModule("01"); // leaves the module while running: stop names the old module
    REQUIRE(sink.events.size() == 7);
    REQUIRE(sink.events[6].type == stats::event::routineModuleStop);
    REQUIRE(sink.events[6].payload["module"] == "02");
    REQUIRE(sink.events[6].payload["mode"] == "diatonic-loop");
    REQUIRE(sink.events[6].payload["elapsed"] == 180);
    timer.toggle();
    REQUIRE(sink.events.size() == 8);
    REQUIRE(sink.events[7].payload["module"] == "01");
    REQUIRE(sink.events[7].payload["mode"] == "diagonal");
}
