// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Routine timer state machine (routine-spec.md section 1).
#include "core/routine/timer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cc::routine;

TEST_CASE("routine timer: enter, start, pause, resume, finish, reset; elapsed accumulates per module")
{
    RoutineTimer timer;
    timer.enterModule(1, 600);
    REQUIRE(timer.state() == TimerState::Idle);
    REQUIRE(timer.remainingSeconds() == 600);
    REQUIRE_FALSE(timer.advance(5)); // idle: nothing moves
    REQUIRE(timer.remainingSeconds() == 600);

    timer.toggle();
    REQUIRE(timer.state() == TimerState::Running);
    REQUIRE_FALSE(timer.advance(100));
    REQUIRE(timer.remainingSeconds() == 500);
    timer.toggle();
    REQUIRE(timer.state() == TimerState::Paused);
    REQUIRE_FALSE(timer.advance(100));
    REQUIRE(timer.remainingSeconds() == 500);
    timer.toggle();
    REQUIRE(timer.advance(700)); // overshoot clamps to the segment
    REQUIRE(timer.state() == TimerState::Finished);
    REQUIRE(timer.remainingSeconds() == 0);
    REQUIRE(timer.elapsedSeconds(1) == 600);
    REQUIRE_FALSE(timer.advance(10)); // finished: no more counting

    timer.reset();
    REQUIRE(timer.state() == TimerState::Idle);
    REQUIRE(timer.remainingSeconds() == 600);
    REQUIRE(timer.elapsedSeconds(1) == 600); // already practised time stays

    timer.enterModule(0, 60);
    REQUIRE(timer.module() == 0);
    timer.start();
    REQUIRE(timer.advance(30) == false);
    REQUIRE(timer.elapsedSeconds(0) == 30);
    REQUIRE(timer.elapsedSeconds(1) == 600);
    REQUIRE(timer.elapsedSeconds(9) == 0);
}

TEST_CASE("routine timer: starting a finished segment restarts it; a zero-length segment never runs")
{
    RoutineTimer timer;
    timer.enterModule(2, 10);
    timer.start();
    REQUIRE(timer.advance(10));
    timer.start();
    REQUIRE(timer.state() == TimerState::Running);
    REQUIRE(timer.remainingSeconds() == 10);
    timer.enterModule(3, 0);
    timer.start();
    REQUIRE(timer.state() == TimerState::Idle);
}
