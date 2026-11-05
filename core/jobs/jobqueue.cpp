// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/jobs/jobqueue.h"

#include <algorithm>
#include <exception>
#include <string>

namespace cc::jobs
{

namespace
{

using Clock = std::chrono::steady_clock;

std::chrono::milliseconds since(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
}

} // namespace

std::optional<std::chrono::milliseconds> estimateRemaining(std::optional<double> fraction,
                                                           std::chrono::milliseconds elapsed)
{
    if (!fraction || *fraction < minFractionForEstimate || *fraction >= 1.0) {
        return std::nullopt;
    }
    const double total = static_cast<double>(elapsed.count()) / *fraction;
    return std::chrono::milliseconds{static_cast<long long>(total - static_cast<double>(elapsed.count()))};
}

JobQueue::JobQueue(Listener listener)
    : m_listener(std::move(listener))
    , m_thread([this](const std::stop_token &quit) {
        run(quit);
    })
{
}

JobQueue::~JobQueue()
{
    cancelAll();
    m_thread.request_stop();
    m_wake.notify_all();
    m_thread.join();
}

/// The Queued event comes from the calling thread, right away; everything after from the worker.
JobId JobQueue::enqueue(JobKind kind, Work work, std::optional<std::chrono::milliseconds> timeout)
{
    Entry entry{0, kind, std::move(work), timeout, std::stop_source{}};
    {
        const std::scoped_lock lock(m_mutex);
        entry.id = m_nextId++;
        m_queue.push_back(entry);
    }
    notify(entry, JobState::Queued, Progress{});
    m_wake.notify_one();
    return entry.id;
}

void JobQueue::cancel(JobId id)
{
    const std::scoped_lock lock(m_mutex);
    if (m_runningId == id && m_runningStop) {
        m_runningStop->request_stop();
        return;
    }
    const auto it = std::ranges::find(m_queue, id, &Entry::id);
    if (it != m_queue.end()) {
        it->stop.request_stop();
    }
}

void JobQueue::cancelAll()
{
    const std::scoped_lock lock(m_mutex);
    if (m_runningStop) {
        m_runningStop->request_stop();
    }
    for (Entry &entry : m_queue) {
        entry.stop.request_stop();
    }
}

std::size_t JobQueue::pending() const
{
    const std::scoped_lock lock(m_mutex);
    return m_queue.size() + (m_runningId != 0 ? 1 : 0);
}

void JobQueue::run(const std::stop_token &quit)
{
    try {
        while (auto entry = take(quit)) {
            runOne(*entry);
        }
        // Quitting: whatever is still queued finishes as cancelled, so no listener waits forever.
        while (auto entry = take(std::stop_token{})) {
            entry->stop.request_stop();
            runOne(*entry);
        }
    } catch (...) { // NOLINT(bugprone-empty-catch) a listener must not throw; nothing sensible remains to do
    }
}

/// Blocks until a job is queued or `quit` asks to stop; a default token never asks, so it only returns a job
/// (callers check for an empty queue themselves).
std::optional<JobQueue::Entry> JobQueue::take(const std::stop_token &quit)
{
    std::unique_lock lock(m_mutex);
    if (quit.stop_possible()) {
        if (!m_wake.wait(lock, quit, [this] {
                return !m_queue.empty();
            })) {
            return std::nullopt;
        }
    } else if (m_queue.empty()) {
        return std::nullopt;
    }
    Entry entry = std::move(m_queue.front());
    m_queue.pop_front();
    m_runningStop = entry.stop;
    m_runningId = entry.id;
    return entry;
}

void JobQueue::runOne(Entry &entry)
{
    const Clock::time_point started = Clock::now();
    std::optional<Error> error = entry.stop.stop_requested()
        ? std::optional{Error{ErrorCode::Cancelled, {}, "cancelled before it started"}}
        : runWork(entry, started);
    {
        const std::scoped_lock lock(m_mutex);
        m_runningStop.reset();
        m_runningId = 0;
    }
    notify(entry, JobState::Finished, Progress{std::nullopt, since(started), std::nullopt}, std::move(error));
}

/// Runs the work; a cancel that the timeout triggered comes back as Timeout.
std::optional<Error> JobQueue::runWork(Entry &entry, Clock::time_point started)
{
    notify(entry, JobState::Running, Progress{});
    bool timedOut = false;
    Result<void> result = execute(entry, started, timedOut);
    if (result) {
        return std::nullopt;
    }
    Error error = std::move(result.error());
    if (error.code == ErrorCode::Cancelled && timedOut) {
        error.code = ErrorCode::Timeout;
    }
    return error;
}

Result<void> JobQueue::execute(Entry &entry, Clock::time_point started, bool &timedOut)
{
    std::optional<Clock::time_point> lastEmit;
    const Report report = [&](double fraction) {
        const std::chrono::milliseconds elapsed = since(started);
        if (entry.timeout && elapsed > *entry.timeout && !timedOut) {
            timedOut = true;
            entry.stop.request_stop();
        }
        if (lastEmit && Clock::now() - *lastEmit < progressInterval) {
            return;
        }
        lastEmit = Clock::now();
        const double clamped = std::clamp(fraction, 0.0, 1.0);
        notify(entry, JobState::Running, Progress{clamped, elapsed, estimateRemaining(clamped, elapsed)});
    };
    try {
        return entry.work(entry.stop.get_token(), report);
    } catch (const std::exception &e) {
        // A throwing job is a bug; surfacing it beats losing it.
        return fail(ErrorCode::Unsupported, "job", std::string("job threw: ") + e.what());
    }
}

void JobQueue::notify(const Entry &entry, JobState state, Progress progress, std::optional<Error> error)
{
    if (m_listener) {
        m_listener(JobEvent{entry.id, entry.kind, state, progress, std::move(error)});
    }
}

} // namespace cc::jobs
