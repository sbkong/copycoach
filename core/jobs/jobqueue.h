// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/jobs/job.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace cc::jobs
{

/// Every event the queue emits; always called from the worker thread, never while the queue's lock is held.
using Listener = std::function<void(const JobEvent &)>;

/// Runs jobs one at a time, in order, on its own thread. Cancel requests stop on the running job's token or
/// drops a queued job (which then finishes as Cancelled without running). A job past its timeout is stopped the
/// same way and finishes as Timeout; the check happens on each progress report. Destroying the queue cancels
/// everything and joins. Thread ownership: enqueue / cancel / pending from any thread; the listener from the
/// worker thread.
class JobQueue
{
public:
    static constexpr std::chrono::milliseconds progressInterval{100}; // at most ten progress events a second

    explicit JobQueue(Listener listener);
    ~JobQueue();
    JobQueue(const JobQueue &) = delete;
    JobQueue &operator=(const JobQueue &) = delete;
    JobQueue(JobQueue &&) = delete;
    JobQueue &operator=(JobQueue &&) = delete;

    JobId enqueue(JobKind kind, Work work, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    void cancel(JobId id);
    void cancelAll();
    /// Queued plus running.
    [[nodiscard]] std::size_t pending() const;

private:
    struct Entry {
        JobId id = 0;
        JobKind kind = JobKind::StemSeparation;
        Work work;
        std::optional<std::chrono::milliseconds> timeout;
        std::stop_source stop;
    };

    void run(const std::stop_token &quit);
    std::optional<Entry> take(const std::stop_token &quit);
    void runOne(Entry &entry);
    std::optional<Error> runWork(Entry &entry, std::chrono::steady_clock::time_point started);
    Result<void> execute(Entry &entry, std::chrono::steady_clock::time_point started, bool &timedOut);
    void notify(const Entry &entry, JobState state, Progress progress, std::optional<Error> error = std::nullopt);

    Listener m_listener;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Entry> m_queue;
    std::optional<std::stop_source> m_runningStop; // the running job's token, for cancel
    JobId m_runningId = 0;
    JobId m_nextId = 1;
    std::jthread m_thread; // last: stops and joins before the rest goes
};

} // namespace cc::jobs
