// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// stats.db (stats.md sections 4, 7): sessions, events, crash recovery, the first query.
#include "core/stats/sqlitestore.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace cc::stats;
using namespace std::chrono;

namespace
{

struct FakeClock {
    system_clock::time_point now = sys_days{2026y / 11 / 2} + 9h;

    [[nodiscard]] Clock fn()
    {
        return [this] {
            return now;
        };
    }
};

std::filesystem::path freshDb(const char *name)
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "stats";
    std::filesystem::create_directories(dir);
    const auto file = dir / (std::string(name) + ".db");
    std::filesystem::remove(file);
    return file;
}

StatsEvent tick(const char *module, int elapsed)
{
    return {std::string(event::routineTimerTick), {{"module", module}, {"elapsed", elapsed}}};
}

StatsEvent stop(const char *module, int elapsed)
{
    return {std::string(event::routineModuleStop), {{"module", module}, {"elapsed", elapsed}}};
}

} // namespace

TEST_CASE("sqlite stats store: sessions and events carry UTC times; module seconds come from the running totals")
{
    FakeClock clock;
    SqliteStatsStore store(clock.fn());
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.isOpen());
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record({std::string(event::routineModuleStart), {{"module", "02"}, {"mode", "diatonic-loop"}}});
    clock.now += 60s;
    store.record(tick("02", 60));
    clock.now += 30s;
    store.record(stop("02", 90));
    store.record(stop("01", 20));
    REQUIRE(store.lastError().empty());
    REQUIRE(store.endSession());

    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(tick("02", 60)); // a second session: its own running total adds up
    const auto seconds = store.routineSecondsPerModule(sys_days{2026y / 11 / 1});
    REQUIRE(seconds);
    REQUIRE(seconds->at("02") == 150);
    REQUIRE(seconds->at("01") == 20);
    const auto later = store.routineSecondsPerModule(clock.now + 1h);
    REQUIRE(later);
    REQUIRE(later->empty());

    const auto sessions = store.sessions();
    REQUIRE(sessions);
    REQUIRE(sessions->size() == 2);
    REQUIRE((*sessions)[0].kind == "routine");
    REQUIRE((*sessions)[0].startedAt == "2026-11-02T09:00:00Z");
    REQUIRE((*sessions)[0].endedAt == "2026-11-02T09:01:30Z");
    REQUIRE((*sessions)[1].endedAt.empty());
}

TEST_CASE("sqlite stats store: a session a crash left open is closed at its last heartbeat on the next open")
{
    const auto file = freshDb("crash");
    FakeClock clock;
    {
        SqliteStatsStore store(clock.fn());
        REQUIRE(store.open(file));
        REQUIRE(store.beginSession("routine", "0.1.0-test"));
        clock.now += 5min;
        store.record(tick("03", 300));
        // no endSession(): the process died here
    }
    SqliteStatsStore reopened(clock.fn());
    REQUIRE(reopened.open(file));
    const auto sessions = reopened.sessions();
    REQUIRE(sessions);
    REQUIRE(sessions->size() == 1);
    REQUIRE((*sessions)[0].endedAt == "2026-11-02T09:05:00Z");
    const auto seconds = reopened.routineSecondsPerModule(sys_days{2026y / 11 / 2});
    REQUIRE(seconds);
    REQUIRE(seconds->at("03") == 300);
}

TEST_CASE("sqlite stats store: without a store or session nothing is written and the reason is kept")
{
    SqliteStatsStore store;
    REQUIRE_FALSE(store.isOpen());
    REQUIRE_FALSE(store.beginSession("routine", "0.1.0-test"));
    store.record(tick("01", 60));
    REQUIRE_FALSE(store.lastError().empty());
    REQUIRE_FALSE(store.sessions());
}
