// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <array>
#include <cstdint>

// Routine timer state machine (routine-spec.md section 1 "타이머", routine.md section 5): one segment per module,
// entered with that module's default minutes, then start / pause / reset by the user. Elapsed seconds per module
// accumulate for the session (the "진행" rows); the clock that drives advance() lives in the UI.
namespace cc::routine
{

enum class TimerState : std::uint8_t {
    Idle, // segment loaded, not started
    Running,
    Paused,
    Finished, // remaining reached zero; stays until reset or another module
};

class RoutineTimer
{
public:
    static constexpr int moduleCount = 4;

    /// Enters a module: loads its segment length and goes Idle. Elapsed time of every module is kept.
    void enterModule(int module, int lengthSeconds);
    void start();
    void pause();
    void toggle();
    /// Back to the full segment, Idle. Elapsed time is kept (it already happened).
    void reset();
    /// Advances a running timer by `seconds`; returns true when this call finished the segment.
    bool advance(int seconds);

    [[nodiscard]] TimerState state() const;
    [[nodiscard]] int module() const;
    [[nodiscard]] int remainingSeconds() const;
    [[nodiscard]] int lengthSeconds() const;
    [[nodiscard]] int elapsedSeconds(int module) const;

private:
    TimerState m_state = TimerState::Idle;
    int m_module = 0;
    int m_length = 0;
    int m_remaining = 0;
    std::array<int, moduleCount> m_elapsed{};
};

} // namespace cc::routine
