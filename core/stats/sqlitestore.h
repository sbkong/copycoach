// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/stats/event.h"
#include "core/stats/reader.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

// stats.db (stats.md section 4): one `session` row per app run and `event` rows under it, all times UTC ISO-8601.
// The composition root opens it once; tests open ":memory:". Every record() also stamps the session's
// heartbeat, so a crash loses at most the time since the last event (the routine ticks once a minute).
namespace cc::stats
{

using Clock = std::function<std::chrono::system_clock::time_point()>;

struct SessionInfo {
    long long id = 0;
    std::string kind;
    std::string startedAt;
    std::string endedAt; // empty while the session is open
};

class SqliteStatsStore final : public IStatsSink, public IStatsReader
{
public:
    static constexpr int schemaVersion = 1;

    /// `zone` decides which local date an event belongs to (stats.md section 9); nullptr = the system zone.
    /// Tests inject both the clock and a fixed zone.
    explicit SqliteStatsStore(Clock clock = &systemClock, const std::chrono::time_zone *zone = nullptr);
    ~SqliteStatsStore() override;
    SqliteStatsStore(const SqliteStatsStore &) = delete;
    SqliteStatsStore &operator=(const SqliteStatsStore &) = delete;
    SqliteStatsStore(SqliteStatsStore &&) = delete;
    SqliteStatsStore &operator=(SqliteStatsStore &&) = delete;

    /// Opens or creates the database, checks its schema version and closes the sessions a crash left open
    /// (ended_at = last heartbeat, stats.md section 7).
    Result<void> open(const std::filesystem::path &path);
    /// Starts the session record() writes into; `kind` is "routine" or "copy".
    Result<void> beginSession(std::string_view kind, std::string_view appVersion);
    Result<void> endSession();
    /// Writes the event under the open session; without one, or after a failure, the event is dropped and
    /// lastError() tells why (recording never interrupts practice).
    void record(const StatsEvent &event) override;

    /// Seconds practised per module id ("01".."04") since `since`: the largest `elapsed` each session reported
    /// per module (ticks and stops carry the running total), summed over sessions.
    [[nodiscard]] Result<SecondsPerModule> routineSecondsPerModule(TimePoint since) const override;
    [[nodiscard]] Result<SecondsPerDay> routineSecondsPerDay() const override;
    [[nodiscard]] Result<KeyCoverage> routineKeyCoverage(TimePoint since) const override;
    [[nodiscard]] Result<std::vector<SessionInfo>> sessions() const;

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] const std::string &lastError() const;

    static TimePoint systemClock();
    /// "2026-11-02T09:30:00Z"
    static std::string formatUtc(TimePoint time);

private:
    struct Closer {
        void operator()(sqlite3 *db) const;
    };

    [[nodiscard]] Result<void> exec(std::string_view sql) const;
    [[nodiscard]] Result<void> migrate() const;
    [[nodiscard]] std::string now() const;

    Clock m_clock;
    const std::chrono::time_zone *m_zone;
    std::unique_ptr<sqlite3, Closer> m_db;
    std::string m_path; // the error subject
    long long m_sessionId = 0; // 0: no open session
    std::string m_lastError;
};

} // namespace cc::stats
