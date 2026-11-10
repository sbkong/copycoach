// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// stats.db (stats.md sections 4, 7, 9): sessions, events, crash recovery, the queries the dock draws.
#include "core/stats/sqlitestore.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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

StatsEvent start(const char *module, nlohmann::json keys)
{
    return {std::string(event::routineModuleStart),
            {{"module", module}, {"mode", "diatonic-loop"}, {"keys", std::move(keys)}, {"tempo", 60}}};
}

constexpr int minDaySeconds = 5 * 60;

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

TEST_CASE("sqlite stats store: seconds per day follow the store's zone and split a module that runs past midnight")
{
    const std::chrono::time_zone *seoul = std::chrono::locate_zone("Asia/Seoul"); // UTC+9, no DST
    FakeClock clock;
    clock.now = sys_days{2026y / 11 / 1} + 14h + 50min; // 2026-11-01 23:50 in Seoul
    SqliteStatsStore store(clock.fn(), seoul);
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(start("02", {"C", "F"}));
    clock.now += 5min;
    store.record(tick("02", 300)); // 23:55 local: Nov 1
    clock.now += 10min;
    store.record(tick("02", 900)); // 00:05 local: the next 600 s belong to Nov 2
    clock.now += 1min;
    store.record(stop("02", 960));
    store.record(stop("01", 100)); // another module, same session, same day

    const auto perDay = store.routineSecondsPerDay();
    REQUIRE(perDay);
    REQUIRE(perDay->size() == 2);
    REQUIRE(perDay->at(local_days{2026y / 11 / 1}) == 300);
    REQUIRE(perDay->at(local_days{2026y / 11 / 2}) == 760);

    SqliteStatsStore utc(clock.fn(), std::chrono::locate_zone("UTC"));
    REQUIRE(utc.open(":memory:"));
    REQUIRE(utc.beginSession("routine", "0.1.0-test"));
    utc.record(tick("02", 300)); // 15:06Z: all of it is still Nov 1 in UTC
    const auto utcDays = utc.routineSecondsPerDay();
    REQUIRE(utcDays);
    REQUIRE(utcDays->size() == 1);
    REQUIRE(utcDays->at(local_days{2026y / 11 / 1}) == 300);
}

TEST_CASE("sqlite stats store: today's module seconds count only the growth reported since local midnight")
{
    const std::chrono::time_zone *seoul = std::chrono::locate_zone("Asia/Seoul");
    FakeClock clock;
    clock.now = sys_days{2026y / 11 / 1} + 14h + 50min; // 2026-11-01 23:50 in Seoul
    SqliteStatsStore store(clock.fn(), seoul);
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    clock.now += 5min;
    store.record(tick("02", 300)); // 23:55 local: Nov 1
    clock.now += 10min;
    store.record(tick("02", 900)); // 00:05 local: Nov 2
    clock.now += 1min;
    store.record(stop("02", 960));

    const auto today = store.routineSecondsPerModule(startOfLocalDay(local_days{2026y / 11 / 2}, seoul));
    REQUIRE(today);
    REQUIRE(today->at("02") == 660); // 600 + 60 reported after midnight; the first 300 belong to Nov 1
    const auto whole = store.routineSecondsPerModule(sys_days{2026y / 11 / 1});
    REQUIRE(whole);
    REQUIRE(whole->at("02") == 960);
}

TEST_CASE("sqlite stats store: key coverage counts the keys of module starts in the window, per module")
{
    FakeClock clock;
    SqliteStatsStore store(clock.fn());
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(start("02", {"C", "F"}));
    clock.now += 1h;
    store.record(start("02", {"C"}));
    store.record(start("03", {"Db"}));
    store.record({std::string(event::routineModuleStart), {{"module", "04"}}}); // no keys: ignored

    const auto coverage = store.routineKeyCoverage(sys_days{2026y / 11 / 1});
    REQUIRE(coverage);
    REQUIRE(coverage->size() == 2);
    REQUIRE(coverage->at("02").at("C") == 2);
    REQUIRE(coverage->at("02").at("F") == 1);
    REQUIRE(coverage->at("03").at("Db") == 1);
    const auto recent = store.routineKeyCoverage(clock.now - 1min);
    REQUIRE(recent);
    REQUIRE(recent->at("02").at("C") == 1);
    REQUIRE_FALSE(recent->at("02").contains("F"));
}

TEST_CASE("sqlite stats store: copy loop stops add up per project and per range, in time order")
{
    FakeClock clock;
    SqliteStatsStore store(clock.fn());
    REQUIRE(store.open(":memory:"));
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    const auto loopStop = [](const char *project, nlohmann::json range, int duration, int repeats, int speed) {
        return StatsEvent{std::string(event::copyLoopStop),
                          {{"project_id", project},
                           {"range_id", std::move(range)},
                           {"bars", nullptr},
                           {"duration", duration},
                           {"repeats", repeats},
                           {"speed_pct", speed}}};
    };
    store.record(loopStop("p1", "r1", 60, 3, 70));
    clock.now += 1h;
    store.record(loopStop("p1", "r1", 30, 1, 85));
    clock.now += 1h;
    store.record(loopStop("p1", nullptr, 100, 0, 100));
    store.record(loopStop("p2", "r9", 999, 9, 50)); // another song

    const auto all = store.copyProjectSeconds("p1", sys_days{2026y / 1 / 1});
    REQUIRE(all);
    REQUIRE(*all == 190);
    const auto later = store.copyProjectSeconds("p1", sys_days{2026y / 11 / 2} + 10h + 30min);
    REQUIRE(later);
    REQUIRE(*later == 100);

    const auto loops = store.copyLoopStats("p1");
    REQUIRE(loops);
    REQUIRE(loops->size() == 2);
    const CopyLoopStats &r1 = loops->at("r1");
    REQUIRE(r1.plays == 2);
    REQUIRE(r1.seconds == 90);
    REQUIRE(r1.repeats == 4);
    REQUIRE(r1.maxSpeedPct == 85);
    REQUIRE(r1.lastSpeedPct == 85);
    const CopyLoopStats &free = loops->at("");
    REQUIRE(free.plays == 1);
    REQUIRE(free.seconds == 100);
    REQUIRE(store.copyLoopStats("nope")->empty());
}

TEST_CASE("sqlite stats store: stage seconds run from each open or stage entry to the next one or the session end")
{
    FakeClock clock;
    SqliteStatsStore store(clock.fn());
    REQUIRE(store.open(":memory:"));
    const auto open = [](const char *project, int stage) {
        return StatsEvent{std::string(event::copyProjectOpen), {{"project_id", project}, {"stage", stage}}};
    };
    const auto enter = [](const char *project, int stage) {
        return StatsEvent{std::string(event::copyStageEnter), {{"project_id", project}, {"stage", stage}}};
    };
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(open("p1", 1));
    clock.now += 10min;
    store.record(enter("p1", 4)); // stage 1: 600 s
    clock.now += 15min;
    store.record(open("p2", 2)); // stage 4: 900 s; p2 from here is not p1's
    clock.now += 5min;
    REQUIRE(store.endSession());

    clock.now += 1h;
    REQUIRE(store.beginSession("routine", "0.1.0-test"));
    store.record(open("p1", 4));
    clock.now += 5min; // the session is still open: it ends "now"

    const auto seconds = store.copyStageSeconds("p1");
    REQUIRE(seconds);
    REQUIRE(seconds->size() == 2);
    REQUIRE(seconds->at(1) == 600);
    REQUIRE(seconds->at(4) == 1200);
    const auto other = store.copyStageSeconds("p2");
    REQUIRE(other);
    REQUIRE(other->at(2) == 300);
    REQUIRE(store.copyStageSeconds("nope")->empty());
}

TEST_CASE("streak: consecutive days with at least the minimum, today or yesterday as the end")
{
    const local_days today{2026y / 11 / 2};
    SecondsPerDay perDay;
    REQUIRE(streakDays(perDay, today, minDaySeconds) == 0);
    perDay[today - days{3}] = minDaySeconds;
    perDay[today - days{2}] = minDaySeconds + 1;
    perDay[today - days{1}] = minDaySeconds;
    REQUIRE(streakDays(perDay, today, minDaySeconds) == 3); // nothing yet today: still alive from yesterday
    perDay[today] = minDaySeconds - 1; // today practised, but under the minimum
    REQUIRE(streakDays(perDay, today, minDaySeconds) == 3);
    perDay[today] = minDaySeconds;
    REQUIRE(streakDays(perDay, today, minDaySeconds) == 4);
    perDay[today - days{2}] = 0; // a gap two days back cuts it
    REQUIRE(streakDays(perDay, today, minDaySeconds) == 2);
    perDay.erase(today);
    perDay.erase(today - days{1});
    REQUIRE(streakDays(perDay, today, minDaySeconds) == 0); // the day before yesterday does not keep it alive
}

TEST_CASE("local day helpers: the day of an instant and the instant a day starts, in the given zone")
{
    const std::chrono::time_zone *seoul = std::chrono::locate_zone("Asia/Seoul");
    const TimePoint lateUtc = sys_days{2026y / 11 / 1} + 16h; // 01:00 Nov 2 in Seoul
    REQUIRE(localDayOf(lateUtc, seoul) == local_days{2026y / 11 / 2});
    REQUIRE(localDayOf(lateUtc, std::chrono::locate_zone("UTC")) == local_days{2026y / 11 / 1});
    REQUIRE(startOfLocalDay(local_days{2026y / 11 / 2}, seoul) == sys_days{2026y / 11 / 1} + 15h);
}

TEST_CASE("sqlitestore: exportCsv writes a header and one quoted row per event")
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "stats-export";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    SqliteStatsStore store;
    REQUIRE(store.open(dir / "stats.db"));
    REQUIRE(store.beginSession("routine", "0.1.0"));
    store.record(StatsEvent{std::string(event::routineModuleStart), {{"module", "01"}, {"mode", "ionian"}}});
    REQUIRE(store.endSession());
    const auto csv = dir / "out.csv";
    REQUIRE(store.exportCsv(csv));
    std::ifstream file(csv);
    std::string header;
    std::string row;
    REQUIRE(std::getline(file, header));
    REQUIRE(std::getline(file, row));
    CHECK(header == "id,session,kind,project,at,type,payload");
    CHECK(row.contains("\"routine.module.start\""));
    CHECK(row.contains("\"\"module\"\"")); // the payload's quotes are doubled
}
