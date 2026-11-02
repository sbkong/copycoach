// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/routine/timer.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <string>

// Drives core's RoutineTimer with a one-second clock, follows the session's module, and reads the segment
// lengths from practice.timer-minutes. The toolbar chip, the navigator rows and the status bar subscribe.
namespace cc::ui
{

class RoutineSession;
class Settings;

class RoutineTimerController : public QObject
{
    Q_OBJECT
public:
    RoutineTimerController(Settings &settings, RoutineSession &session, QObject *parent = nullptr);

    [[nodiscard]] const routine::RoutineTimer &timer() const;
    [[nodiscard]] int currentModule() const;
    /// "mm:ss" of the remaining time.
    [[nodiscard]] QString remainingText() const;
    [[nodiscard]] QString moduleLabel(int module) const;
    [[nodiscard]] int lengthSeconds(int module) const;
    /// "01".."04" -> 0..3; anything else -> 0.
    [[nodiscard]] static int moduleIndexOf(const std::string &moduleId);

    void toggle();
    void reset();
    /// Advances by whole seconds; the clock calls it with 1, tests with more.
    void advance(int seconds);

signals:
    void changed();
    void segmentFinished(int module);

private:
    void followSession();

    Settings *m_settings;
    RoutineSession *m_session;
    routine::RoutineTimer m_timer;
    QTimer m_clock;
};

} // namespace cc::ui
