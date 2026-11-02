// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/stats/sqlitestore.h"

#include <sqlite3.h>

#include <algorithm>
#include <format>
#include <utility>

namespace cc::stats
{

namespace
{

using Statement = std::unique_ptr<sqlite3_stmt, int (*)(sqlite3_stmt *)>;

constexpr std::string_view schemaSql = R"(
CREATE TABLE IF NOT EXISTS meta(schema_version INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS session(
    id INTEGER PRIMARY KEY, kind TEXT NOT NULL, started_at TEXT NOT NULL, ended_at TEXT,
    project_id TEXT, app_version TEXT NOT NULL, heartbeat_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS event(
    id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES session(id),
    at TEXT NOT NULL, type TEXT NOT NULL, payload TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS event_type_at ON event(type, at);
)";

Result<Statement> prepare(sqlite3 *db, std::string_view sql, const std::string &subject)
{
    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr) != SQLITE_OK) {
        return fail(ErrorCode::FileCorrupt, subject, sqlite3_errmsg(db));
    }
    return Statement(raw, &sqlite3_finalize);
}

/// Binds UTF-8 text without copying: the caller keeps it alive until the statement has stepped.
void bindText(sqlite3_stmt *stmt, int index, std::string_view text)
{
    sqlite3_bind_text(stmt, index, text.data(), static_cast<int>(text.size()), nullptr);
}

std::string columnText(sqlite3_stmt *stmt, int column)
{
    const auto *bytes = static_cast<const char *>(sqlite3_column_blob(stmt, column));
    if (bytes == nullptr) {
        return {};
    }
    return {bytes, static_cast<std::size_t>(sqlite3_column_bytes(stmt, column))};
}

/// Runs a statement that returns no rows.
Result<void> stepDone(sqlite3 *db, sqlite3_stmt *stmt, const std::string &subject)
{
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        return fail(ErrorCode::WriteFailed, subject, sqlite3_errmsg(db));
    }
    return {};
}

/// Hands every row to `onRow`; a step error becomes the Result.
template<typename OnRow>
Result<void> forEachRow(sqlite3 *db, sqlite3_stmt *stmt, const std::string &subject, OnRow onRow)
{
    int step = sqlite3_step(stmt);
    while (step == SQLITE_ROW) {
        onRow(stmt);
        step = sqlite3_step(stmt);
    }
    if (step != SQLITE_DONE) {
        return fail(ErrorCode::FileCorrupt, subject, sqlite3_errmsg(db));
    }
    return {};
}

std::string utf8(const std::filesystem::path &path)
{
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

} // namespace

void SqliteStatsStore::Closer::operator()(sqlite3 *db) const
{
    sqlite3_close(db);
}

SqliteStatsStore::SqliteStatsStore(Clock clock)
    : m_clock(std::move(clock))
{
}

SqliteStatsStore::~SqliteStatsStore() = default;

TimePoint SqliteStatsStore::systemClock()
{
    return std::chrono::system_clock::now();
}

std::string SqliteStatsStore::formatUtc(TimePoint time)
{
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(time));
}

std::string SqliteStatsStore::now() const
{
    return formatUtc(m_clock());
}

bool SqliteStatsStore::isOpen() const
{
    return m_db != nullptr;
}

const std::string &SqliteStatsStore::lastError() const
{
    return m_lastError;
}

Result<void> SqliteStatsStore::exec(std::string_view sql) const
{
    const std::string text(sql);
    char *message = nullptr;
    if (sqlite3_exec(m_db.get(), text.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        std::string detail = message == nullptr ? std::string() : std::string(message);
        sqlite3_free(message);
        return fail(ErrorCode::WriteFailed, m_path, std::move(detail));
    }
    return {};
}

Result<void> SqliteStatsStore::open(const std::filesystem::path &path)
{
    m_path = utf8(path);
    sqlite3 *raw = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(m_path.c_str(), &raw, flags, nullptr) != SQLITE_OK) {
        const std::string detail = raw == nullptr ? std::string("out of memory") : std::string(sqlite3_errmsg(raw));
        sqlite3_close(raw);
        return fail(ErrorCode::FileCorrupt, m_path, detail);
    }
    m_db.reset(raw);
    if (auto r = migrate(); !r) {
        m_db.reset();
        return r;
    }
    // A session a crash left open ends at its last heartbeat (stats.md section 7).
    return exec("UPDATE session SET ended_at = heartbeat_at WHERE ended_at IS NULL");
}

Result<void> SqliteStatsStore::migrate() const
{
    if (auto r = exec(schemaSql); !r) {
        return r;
    }
    auto stmt = prepare(m_db.get(), "SELECT schema_version FROM meta LIMIT 1", m_path);
    if (!stmt) {
        return std::unexpected(stmt.error());
    }
    const int step = sqlite3_step(stmt->get());
    if (step == SQLITE_ROW) {
        const int version = sqlite3_column_int(stmt->get(), 0);
        if (version > schemaVersion) {
            return fail(ErrorCode::SchemaTooNew, m_path, "schema_version " + std::to_string(version));
        }
        return {}; // version 1 is the only one so far; later versions upgrade here, in order
    }
    if (step != SQLITE_DONE) {
        return fail(ErrorCode::FileCorrupt, m_path, sqlite3_errmsg(m_db.get()));
    }
    return exec("INSERT INTO meta(schema_version) VALUES (" + std::to_string(schemaVersion) + ")");
}

Result<void> SqliteStatsStore::beginSession(std::string_view kind, std::string_view appVersion)
{
    if (!isOpen()) {
        return fail(ErrorCode::Unsupported, m_path, "store not open");
    }
    if (auto r = endSession(); !r) {
        return r;
    }
    auto stmt = prepare(m_db.get(),
                        "INSERT INTO session(kind, started_at, app_version, heartbeat_at) VALUES (?, ?, ?, ?)",
                        m_path);
    if (!stmt) {
        return std::unexpected(stmt.error());
    }
    const std::string at = now();
    bindText(stmt->get(), 1, kind);
    bindText(stmt->get(), 2, at);
    bindText(stmt->get(), 3, appVersion);
    bindText(stmt->get(), 4, at);
    if (auto r = stepDone(m_db.get(), stmt->get(), m_path); !r) {
        return r;
    }
    m_sessionId = sqlite3_last_insert_rowid(m_db.get());
    return {};
}

Result<void> SqliteStatsStore::endSession()
{
    if (m_sessionId == 0) {
        return {};
    }
    auto stmt = prepare(m_db.get(), "UPDATE session SET ended_at = ?, heartbeat_at = ? WHERE id = ?", m_path);
    if (!stmt) {
        return std::unexpected(stmt.error());
    }
    const std::string at = now();
    bindText(stmt->get(), 1, at);
    bindText(stmt->get(), 2, at);
    sqlite3_bind_int64(stmt->get(), 3, m_sessionId);
    m_sessionId = 0;
    return stepDone(m_db.get(), stmt->get(), m_path);
}

void SqliteStatsStore::record(const StatsEvent &event)
{
    if (m_sessionId == 0) {
        m_lastError = "no open session";
        return;
    }
    auto insert = prepare(m_db.get(), "INSERT INTO event(session_id, at, type, payload) VALUES (?, ?, ?, ?)", m_path);
    auto heartbeat = prepare(m_db.get(), "UPDATE session SET heartbeat_at = ? WHERE id = ?", m_path);
    if (!insert || !heartbeat) {
        m_lastError = insert ? heartbeat.error().detail : insert.error().detail;
        return;
    }
    const std::string at = now();
    const std::string payload = event.payload.dump();
    sqlite3_bind_int64(insert->get(), 1, m_sessionId);
    bindText(insert->get(), 2, at);
    bindText(insert->get(), 3, event.type);
    bindText(insert->get(), 4, payload);
    bindText(heartbeat->get(), 1, at);
    sqlite3_bind_int64(heartbeat->get(), 2, m_sessionId);
    for (sqlite3_stmt *stmt : {insert->get(), heartbeat->get()}) {
        if (auto r = stepDone(m_db.get(), stmt, m_path); !r) {
            m_lastError = r.error().detail;
            return;
        }
    }
    m_lastError.clear();
}

Result<std::map<std::string, int>> SqliteStatsStore::routineSecondsPerModule(TimePoint since) const
{
    if (!isOpen()) {
        return fail(ErrorCode::Unsupported, m_path, "store not open");
    }
    auto stmt = prepare(m_db.get(), "SELECT session_id, payload FROM event WHERE at >= ? AND type IN (?, ?)", m_path);
    if (!stmt) {
        return std::unexpected(stmt.error());
    }
    const std::string from = formatUtc(since);
    bindText(stmt->get(), 1, from);
    bindText(stmt->get(), 2, event::routineTimerTick);
    bindText(stmt->get(), 3, event::routineModuleStop);
    std::map<std::pair<long long, std::string>, int> longest; // (session, module) -> largest running total
    const auto r = forEachRow(m_db.get(), stmt->get(), m_path, [&longest](sqlite3_stmt *row) {
        const nlohmann::json payload = nlohmann::json::parse(columnText(row, 1), nullptr, false);
        if (!payload.is_object()) {
            return;
        }
        int &seconds = longest[{sqlite3_column_int64(row, 0), payload.value("module", std::string())}];
        seconds = std::max(seconds, payload.value("elapsed", 0));
    });
    if (!r) {
        return std::unexpected(r.error());
    }
    std::map<std::string, int> total;
    for (const auto &[key, seconds] : longest) {
        total[key.second] += seconds;
    }
    return total;
}

Result<std::vector<SessionInfo>> SqliteStatsStore::sessions() const
{
    if (!isOpen()) {
        return fail(ErrorCode::Unsupported, m_path, "store not open");
    }
    auto stmt = prepare(m_db.get(), "SELECT id, kind, started_at, ended_at FROM session ORDER BY id", m_path);
    if (!stmt) {
        return std::unexpected(stmt.error());
    }
    std::vector<SessionInfo> rows;
    const auto r = forEachRow(m_db.get(), stmt->get(), m_path, [&rows](sqlite3_stmt *row) {
        rows.push_back(
            SessionInfo{sqlite3_column_int64(row, 0), columnText(row, 1), columnText(row, 2), columnText(row, 3)});
    });
    if (!r) {
        return std::unexpected(r.error());
    }
    return rows;
}

} // namespace cc::stats
