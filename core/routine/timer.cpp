// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/routine/timer.h"

#include <algorithm>

namespace cc::routine
{

void RoutineTimer::enterModule(int module, int lengthSeconds)
{
    m_module = std::clamp(module, 0, moduleCount - 1);
    m_length = std::max(0, lengthSeconds);
    m_remaining = m_length;
    m_state = TimerState::Idle;
}

void RoutineTimer::start()
{
    if (m_state == TimerState::Finished) {
        reset();
    }
    if (m_remaining > 0) {
        m_state = TimerState::Running;
    }
}

void RoutineTimer::pause()
{
    if (m_state == TimerState::Running) {
        m_state = TimerState::Paused;
    }
}

void RoutineTimer::toggle()
{
    if (m_state == TimerState::Running) {
        pause();
    } else {
        start();
    }
}

void RoutineTimer::reset()
{
    m_remaining = m_length;
    m_state = TimerState::Idle;
}

bool RoutineTimer::advance(int seconds)
{
    if (m_state != TimerState::Running || seconds <= 0) {
        return false;
    }
    const int step = std::min(seconds, m_remaining);
    m_remaining -= step;
    m_elapsed[static_cast<std::size_t>(m_module)] += step;
    if (m_remaining == 0) {
        m_state = TimerState::Finished;
        return true;
    }
    return false;
}

TimerState RoutineTimer::state() const
{
    return m_state;
}

int RoutineTimer::module() const
{
    return m_module;
}

int RoutineTimer::remainingSeconds() const
{
    return m_remaining;
}

int RoutineTimer::lengthSeconds() const
{
    return m_length;
}

int RoutineTimer::elapsedSeconds(int module) const
{
    if (module < 0 || module >= moduleCount) {
        return 0;
    }
    return m_elapsed[static_cast<std::size_t>(module)];
}

} // namespace cc::routine
