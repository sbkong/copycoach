// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/jobschip.h"

#include "ui/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QToolButton>

#include <cmath>

namespace cc::ui
{

namespace
{

constexpr int tickMs = 1000;
constexpr int secondsShownAsSeconds = 90; // past that, whole minutes read better
constexpr double percent = 100.0;

QString formatDuration(std::chrono::milliseconds duration)
{
    const auto seconds = static_cast<int>(duration.count() / 1000);
    if (seconds < secondsShownAsSeconds) {
        return QObject::tr("%1 s").arg(seconds);
    }
    return QObject::tr("%1분").arg(std::lround(static_cast<double>(seconds) / 60.0));
}

} // namespace

JobsChip::JobsChip(const Theme &theme, JobService &jobs, QWidget *parent)
    : QWidget(parent)
    , m_jobs(&jobs)
    , m_label(new QLabel(this))
    , m_cancel(new QToolButton(this))
    , m_tick(new QTimer(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme.spacing(theme::Spacing::Control));
    m_label->setFont(theme.font(theme::TypeStyle::Caption));
    m_label->setAccessibleName(tr("진행 중인 작업"));
    m_cancel->setFont(theme.font(theme::TypeStyle::Caption));
    m_cancel->setAutoRaise(true);
    m_cancel->setAccessibleName(tr("작업 취소"));
    layout->addWidget(m_label);
    layout->addWidget(m_cancel);
    connect(m_cancel, &QToolButton::clicked, this, [this] {
        if (const JobService::JobView *job = m_jobs->current(); job != nullptr) {
            m_jobs->cancel(job->id);
        }
    });
    connect(m_jobs, &JobService::changed, this, &JobsChip::refresh);
    m_tick->setInterval(tickMs);
    connect(m_tick, &QTimer::timeout, this, &JobsChip::refresh);
    refresh();
}

QString JobsChip::text() const
{
    return m_label->text();
}

void JobsChip::refresh()
{
    const JobService::JobView *job = m_jobs->current();
    if (job == nullptr) {
        m_tick->stop();
        hide();
        return;
    }
    QString text = describe(*job);
    if (m_jobs->jobs().size() > 1) {
        text = tr("작업 %1개 · %2").arg(m_jobs->jobs().size()).arg(text);
    }
    m_label->setText(text);
    m_cancel->setText(job->cancelling ? tr("취소 중…") : tr("취소"));
    m_cancel->setEnabled(!job->cancelling);
    if (!m_tick->isActive()) {
        m_tick->start();
    }
    show();
}

QString JobsChip::describe(const JobService::JobView &job)
{
    const QString name = JobService::kindLabel(job.kind);
    if (job.state != jobs::JobState::Running) {
        return tr("%1 · 대기").arg(name);
    }
    if (!job.fraction) {
        return tr("%1 · %2").arg(name, formatDuration(job.elapsed()));
    }
    QString text = tr("%1 %2 %").arg(name).arg(std::lround(*job.fraction * percent));
    if (const auto remaining = job.remaining()) {
        text += tr(" · %1 남음").arg(formatDuration(*remaining));
    }
    return text;
}

} // namespace cc::ui
