// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/stats/event.h"

#include <QObject>

#include <string>

// Turns the routine timer's transitions into practice events (routine-spec.md section 1 "기록", stats.md
// section 4): start when the timer runs, a tick at every full minute, stop when it pauses, finishes, resets or
// leaves the module. `elapsed` is the module's running total for this app run. The sink is all it knows
// about recording (stats.md section 5).
namespace cc::ui
{

class RoutineSession;
class RoutineTimerController;

class RoutineRecorder : public QObject
{
    Q_OBJECT
public:
    RoutineRecorder(stats::IStatsSink &sink,
                    const RoutineTimerController &timer,
                    const RoutineSession &session,
                    QObject *parent = nullptr);

private:
    void onTimerChanged();
    void recordStart(int module);
    void recordStop();
    void recordTick();
    [[nodiscard]] std::string moduleId(int module) const;
    [[nodiscard]] int elapsedSeconds() const;

    stats::IStatsSink *m_sink;
    const RoutineTimerController *m_timer;
    const RoutineSession *m_session;
    bool m_running = false;
    int m_module = 0;
    std::string m_mode; // the mode at start, so a stop after a module switch still names it
    int m_minutesReported = 0;
};

} // namespace cc::ui
