// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/jobs/jobservice.h"

#include <QMetaObject>

#include <algorithm>

namespace cc::ui
{

std::chrono::milliseconds JobService::JobView::elapsed() const
{
    if (state != jobs::JobState::Running) {
        return std::chrono::milliseconds{0};
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt);
}

std::optional<std::chrono::milliseconds> JobService::JobView::remaining() const
{
    return jobs::estimateRemaining(fraction, elapsed());
}

JobService::JobService(QObject *parent)
    : QObject(parent)
    , m_queue([this](const jobs::JobEvent &event) {
        // Worker thread: hop to the main thread; the copy keeps the event alive until then.
        QMetaObject::invokeMethod(
            this,
            [this, event] {
                onEvent(event);
            },
            Qt::QueuedConnection);
    })
{
}

JobService::~JobService() = default;

jobs::JobId JobService::enqueue(jobs::JobKind kind, jobs::Work work, std::optional<std::chrono::milliseconds> timeout)
{
    return m_queue.enqueue(kind, std::move(work), timeout);
}

void JobService::cancel(jobs::JobId id)
{
    if (JobView *job = find(id); job != nullptr && !job->cancelling) {
        job->cancelling = true;
        m_queue.cancel(id);
        emit changed();
    }
}

void JobService::cancelAll()
{
    for (JobView &job : m_jobs) {
        job.cancelling = true;
    }
    m_queue.cancelAll();
    emit changed();
}

const std::vector<JobService::JobView> &JobService::jobs() const
{
    return m_jobs;
}

const JobService::JobView *JobService::current() const
{
    const auto running = std::ranges::find(m_jobs, jobs::JobState::Running, &JobView::state);
    if (running != m_jobs.end()) {
        return &*running;
    }
    return m_jobs.empty() ? nullptr : &m_jobs.front();
}

QString JobService::kindLabel(jobs::JobKind kind)
{
    switch (kind) {
    case jobs::JobKind::StemSeparation:
        return tr("스템 분리");
    case jobs::JobKind::ModelDownload:
        return tr("모델 내려받기");
    case jobs::JobKind::MediaExtract:
        return tr("영상 오디오 추출");
    case jobs::JobKind::MediaCopy:
        return tr("미디어 복사");
    case jobs::JobKind::Measure:
        return tr("측정");
    case jobs::JobKind::Prediction:
        return tr("예측");
    case jobs::JobKind::Prerender:
        return tr("선렌더");
    case jobs::JobKind::Export:
        return tr("내보내기");
    }
    return {};
}

void JobService::onEvent(const jobs::JobEvent &event)
{
    JobView *job = find(event.id);
    if (job == nullptr) {
        if (event.state == jobs::JobState::Finished) {
            return; // already forgotten
        }
        JobView view;
        view.id = event.id;
        view.kind = event.kind;
        m_jobs.push_back(view);
        job = &m_jobs.back();
    }
    if (event.state == jobs::JobState::Finished) {
        std::erase_if(m_jobs, [&event](const JobView &view) {
            return view.id == event.id;
        });
        emit changed();
        emit finished(event);
        return;
    }
    if (event.state == jobs::JobState::Running && job->state != jobs::JobState::Running) {
        job->startedAt = std::chrono::steady_clock::now() - event.progress.elapsed;
    }
    job->state = event.state;
    if (event.progress.fraction) {
        job->fraction = event.progress.fraction;
    }
    emit changed();
}

JobService::JobView *JobService::find(jobs::JobId id)
{
    const auto it = std::ranges::find(m_jobs, id, &JobView::id);
    return it == m_jobs.end() ? nullptr : &*it;
}

} // namespace cc::ui
