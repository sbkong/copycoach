// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/jobs/jobqueue.h"

#include <QObject>
#include <QString>

#include <chrono>
#include <optional>
#include <vector>

namespace cc::ui
{

/// The main-thread face of the job queue (coding-standards.md section 8: workers reach the GUI through queued
/// calls). Keeps the live jobs for the status bar, forgets a job once it finishes, and remembers which ones the
/// user is cancelling so the button can say so (states.md section 2). Thread ownership: main thread only.
class JobService final : public QObject
{
    Q_OBJECT
public:
    struct JobView {
        jobs::JobId id = 0;
        jobs::JobKind kind = jobs::JobKind::StemSeparation;
        jobs::JobState state = jobs::JobState::Queued;
        std::optional<double> fraction;
        std::chrono::steady_clock::time_point startedAt; // valid once Running
        bool cancelling = false;

        [[nodiscard]] std::chrono::milliseconds elapsed() const;
        [[nodiscard]] std::optional<std::chrono::milliseconds> remaining() const;
    };

    explicit JobService(QObject *parent = nullptr);
    ~JobService() override;
    Q_DISABLE_COPY_MOVE(JobService)

    jobs::JobId
    enqueue(jobs::JobKind kind, jobs::Work work, std::optional<std::chrono::milliseconds> timeout = std::nullopt);
    void cancel(jobs::JobId id);
    void cancelAll();
    [[nodiscard]] const std::vector<JobView> &jobs() const;
    /// The running job, or the first queued one; what the status bar shows.
    [[nodiscard]] const JobView *current() const;

    static QString kindLabel(jobs::JobKind kind);

signals:
    void changed();
    /// After the job left the list; `error` is set for failures, cancels and timeouts.
    void finished(const jobs::JobEvent &event);

private:
    void onEvent(const jobs::JobEvent &event); // main thread, after the queued hop
    JobView *find(jobs::JobId id);

    std::vector<JobView> m_jobs;
    jobs::JobQueue m_queue; // last: joins its thread before the list goes
};

} // namespace cc::ui
