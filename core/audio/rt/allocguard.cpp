// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/rt/allocguard.h"

#include <atomic>

namespace cc::rt
{

namespace
{

thread_local int s_guardDepth = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) per-thread flag
std::atomic<long long> s_forbiddenCount{0}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

AllocGuard::AllocGuard()
{
    ++s_guardDepth;
}

AllocGuard::~AllocGuard()
{
    --s_guardDepth;
}

bool allocationsForbidden()
{
    return s_guardDepth > 0;
}

void noteAllocation()
{
    if (allocationsForbidden()) {
        s_forbiddenCount.fetch_add(1, std::memory_order_relaxed);
    }
}

long long forbiddenAllocationCount()
{
    return s_forbiddenCount.load(std::memory_order_relaxed);
}

void resetForbiddenAllocationCount()
{
    s_forbiddenCount.store(0, std::memory_order_relaxed);
}

} // namespace cc::rt
