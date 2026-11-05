// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/jobs/jobqueue.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace cc;
using namespace cc::jobs;
using namespace std::chrono_literals;

namespace
{

/// Collects events across threads and lets a test wait for the queue to go quiet.
struct Log {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<JobEvent> events;
    std::thread::id listenerThread;

    Listener listener()
    {
        return [this](const JobEvent &event) {
            {
                const std::scoped_lock lock(mutex);
                events.push_back(event);
                if (event.state != JobState::Queued) {
                    listenerThread = std::this_thread::get_id();
                }
            }
            changed.notify_all();
        };
    }

    /// Blocks until `id` has finished (or 5 s pass); returns that event.
    JobEvent finished(JobId id)
    {
        std::unique_lock lock(mutex);
        JobEvent found;
        const bool ok = changed.wait_for(lock, 5s, [&] {
            for (const JobEvent &event : events) {
                if (event.id == id && event.state == JobState::Finished) {
                    found = event;
                    return true;
                }
            }
            return false;
        });
        REQUIRE(ok);
        return found;
    }

    std::vector<JobState> statesOf(JobId id)
    {
        const std::scoped_lock lock(mutex);
        std::vector<JobState> out;
        for (const JobEvent &event : events) {
            if (event.id == id) {
                out.push_back(event.state);
            }
        }
        return out;
    }
};

/// Reports steps until stopped; finishes on its own after `steps`.
Work stepping(int steps, std::chrono::milliseconds pause)
{
    return [steps, pause](const std::stop_token &stop, const Report &report) -> Result<void> {
        for (int i = 0; i < steps; ++i) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled, "stepping");
            }
            report(static_cast<double>(i) / steps);
            std::this_thread::sleep_for(pause);
        }
        return {};
    };
}

/// The failure code of a finished event; a success has none and fails the test.
ErrorCode codeOf(const JobEvent &event)
{
    REQUIRE(event.error.has_value());
    return event.error.has_value() ? event.error->code : ErrorCode::DiskFull;
}

} // namespace

TEST_CASE("jobs: a job runs on the worker thread, reports progress and finishes in order")
{
    Log log;
    JobQueue queue(log.listener());
    REQUIRE(queue.pending() == 0);
    const JobId first = queue.enqueue(JobKind::Measure, stepping(5, 2ms));
    const JobId second = queue.enqueue(JobKind::Export, stepping(1, 0ms));
    const JobEvent done = log.finished(second);
    REQUIRE_FALSE(done.error);
    REQUIRE(done.kind == JobKind::Export);
    REQUIRE(log.listenerThread != std::this_thread::get_id());

    const auto states = log.statesOf(first);
    REQUIRE(states.size() >= 3);
    REQUIRE(states.front() == JobState::Queued);
    REQUIRE(states[1] == JobState::Running);
    REQUIRE(states.back() == JobState::Finished);
    {
        const std::scoped_lock lock(log.mutex);
        bool firstFinishedBeforeSecondStarted = false;
        for (const JobEvent &event : log.events) {
            if (event.id == first && event.state == JobState::Finished) {
                firstFinishedBeforeSecondStarted = true;
            }
            if (event.id == second && event.state == JobState::Running) {
                REQUIRE(firstFinishedBeforeSecondStarted);
            }
        }
    }
    std::this_thread::sleep_for(5ms);
    REQUIRE(queue.pending() == 0);
}

TEST_CASE("jobs: cancel stops a running job and drops a queued one without running it")
{
    Log log;
    JobQueue queue(log.listener());
    std::atomic<bool> ran = false;
    const JobId running = queue.enqueue(JobKind::StemSeparation, stepping(1000, 1ms));
    const JobId queued = queue.enqueue(JobKind::Prediction, [&ran](const std::stop_token &, const Report &) {
        ran = true;
        return Result<void>{};
    });
    std::this_thread::sleep_for(10ms);
    REQUIRE(queue.pending() == 2);
    queue.cancel(queued);
    queue.cancel(running);
    const JobEvent first = log.finished(running);
    REQUIRE(first.error);
    REQUIRE(codeOf(first) == ErrorCode::Cancelled);
    const JobEvent second = log.finished(queued);
    REQUIRE(second.error);
    REQUIRE(codeOf(second) == ErrorCode::Cancelled);
    REQUIRE_FALSE(ran);
    REQUIRE(log.statesOf(queued) == std::vector{JobState::Queued, JobState::Finished});
}

TEST_CASE("jobs: a job past its timeout is stopped and reported as Timeout")
{
    Log log;
    JobQueue queue(log.listener());
    const JobId id = queue.enqueue(JobKind::ModelDownload, stepping(1000, 1ms), 20ms);
    const JobEvent done = log.finished(id);
    REQUIRE(done.error);
    REQUIRE(codeOf(done) == ErrorCode::Timeout);
    REQUIRE(done.progress.elapsed >= 20ms);
}

TEST_CASE("jobs: destroying the queue cancels the running job, finishes the queued ones and joins")
{
    Log log;
    std::atomic<bool> sawStop = false;
    JobId queued = 0;
    {
        JobQueue queue(log.listener());
        queue.enqueue(JobKind::Prerender, [&sawStop](const std::stop_token &stop, const Report &) -> Result<void> {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(1ms);
            }
            sawStop = true;
            return fail(ErrorCode::Cancelled, "prerender");
        });
        queued = queue.enqueue(JobKind::Export, stepping(1, 0ms));
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(sawStop);
    const JobEvent done = log.finished(queued);
    REQUIRE(done.error);
    REQUIRE(codeOf(done) == ErrorCode::Cancelled);
}

TEST_CASE("jobs: a throwing job is reported as a failure instead of taking the worker down")
{
    Log log;
    JobQueue queue(log.listener());
    const JobId id = queue.enqueue(JobKind::Measure, [](const std::stop_token &, const Report &) -> Result<void> {
        throw std::runtime_error("boom");
    });
    const JobEvent done = log.finished(id);
    REQUIRE(done.error);
    REQUIRE(codeOf(done) == ErrorCode::Unsupported);
    const JobId next = queue.enqueue(JobKind::Measure, stepping(1, 0ms));
    REQUIRE_FALSE(log.finished(next).error);
}

TEST_CASE("jobs: the remaining time is estimated from the fraction once it is meaningful")
{
    REQUIRE_FALSE(estimateRemaining(std::nullopt, 1000ms));
    REQUIRE_FALSE(estimateRemaining(0.01, 1000ms));
    REQUIRE_FALSE(estimateRemaining(1.0, 1000ms));
    REQUIRE(estimateRemaining(0.25, 1000ms) == 3000ms);
    REQUIRE(estimateRemaining(0.5, 4000ms) == 4000ms);
}
