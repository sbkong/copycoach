// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// coding-standards.md section 8 building blocks: SPSC queue, tick clock, allocation guard.
#include "core/audio/rt/allocguard.h"
#include "core/audio/rt/spscqueue.h"
#include "core/audio/rt/tickclock.h"
#include "core/model/timemap.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cc;

TEST_CASE("spsc queue: fifo order, full and empty are reported")
{
    rt::SpscQueue<int, 4> queue;
    REQUIRE(queue.empty());
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE(queue.push(3));
    REQUIRE_FALSE(queue.push(4)); // one slot stays free
    int value = 0;
    REQUIRE(queue.pop(value));
    REQUIRE(value == 1);
    REQUIRE(queue.push(4));
    REQUIRE(queue.pop(value));
    REQUIRE(queue.pop(value));
    REQUIRE(queue.pop(value));
    REQUIRE(value == 4);
    REQUIRE_FALSE(queue.pop(value));
    REQUIRE(queue.empty());
}

TEST_CASE("tick clock: agrees with TimeMap for a constant tempo and rebases on tempo change")
{
    const rt::TickClock clock(48000.0, 120.0);
    const model::TimeMap map(48000.0, {model::TempoSegment{0, 0, 120.0}});
    for (const std::int64_t tick : {0LL, 1LL, 479LL, 960LL, 3840LL, 123456LL}) {
        REQUIRE(clock.sampleAt(tick) == map.tickToSample(tick));
    }
    REQUIRE(clock.tickAt(24000) == 960);
    REQUIRE(clock.tickAt(23999) == 959);

    rt::TickClock changing(48000.0, 120.0);
    changing.setTempoAt(24000, 60.0); // one quarter in, tempo halves
    REQUIRE(changing.tickAt(24000) == 960);
    REQUIRE(changing.sampleAt(1920) == 24000 + 48000);
    REQUIRE(changing.tickAt(24000 + 47999) == 1919);
}

TEST_CASE("tick clock: negative ticks (count-in) floor correctly")
{
    rt::TickClock clock(48000.0, 120.0);
    clock.rebase(0, -3840);
    REQUIRE(clock.sampleAt(0) == 96000);
    REQUIRE(clock.tickAt(0) == -3840);
    REQUIRE(clock.tickAt(1) == -3840);
    REQUIRE(clock.tickAt(24) == -3840);
    REQUIRE(clock.tickAt(25) == -3839);
}

TEST_CASE("alloc guard: counts allocations made under a guard and nothing outside")
{
    rt::resetForbiddenAllocationCount();
    {
        const std::vector<int> outside(16);
        REQUIRE(outside.size() == 16);
        REQUIRE(rt::forbiddenAllocationCount() == 0);
    }
    bool forbiddenInside = false;
    std::size_t insideSize = 0;
    {
        const rt::AllocGuard guard; // no REQUIRE in here: Catch2 itself allocates while asserting
        forbiddenInside = rt::allocationsForbidden();
        const std::vector<int> inside(
            16); // deliberately allocates (twice under the MSVC debug STL: data + iterator proxy)
        insideSize = inside.size();
    }
    REQUIRE(forbiddenInside);
    REQUIRE(insideSize == 16);
    REQUIRE(rt::forbiddenAllocationCount() >= 1);
    REQUIRE_FALSE(rt::allocationsForbidden());
    rt::resetForbiddenAllocationCount();
}
