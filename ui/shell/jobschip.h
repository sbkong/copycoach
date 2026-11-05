// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "ui/jobs/jobservice.h"

#include <QWidget>

class QLabel;
class QTimer;
class QToolButton;

namespace cc::ui
{

class Theme;

/// Status bar chip for the job in front: "스템 분리 62 % · 11 s 남음 [취소]", "작업 3개 · 측정 · 7 s" while the
/// fraction is unknown (states.md section 2). Hidden when nothing runs; the button reads "취소 중…" and
/// disables as soon as it is pressed, until the job reports back.
class JobsChip final : public QWidget
{
    Q_OBJECT
public:
    JobsChip(const Theme &theme, JobService &jobs, QWidget *parent = nullptr);

    [[nodiscard]] QString text() const; // what the label shows, for tests

private:
    void refresh();
    [[nodiscard]] static QString describe(const JobService::JobView &job);

    JobService *m_jobs;
    QLabel *m_label;
    QToolButton *m_cancel;
    QTimer *m_tick; // keeps the elapsed seconds moving between progress events
};

} // namespace cc::ui
