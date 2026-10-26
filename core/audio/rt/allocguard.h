// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

// Allocation guard for the audio callback (coding-standards.md section 8). The guard only raises a thread-local
// flag; the global operator new replacement in allochook.cpp (linked into tests and debug builds) counts every
// allocation made while the flag is up. Release builds without the hook pay nothing but the flag.
namespace cc::rt
{

class AllocGuard
{
public:
    AllocGuard();
    ~AllocGuard();
    AllocGuard(const AllocGuard &) = delete;
    AllocGuard &operator=(const AllocGuard &) = delete;
    AllocGuard(AllocGuard &&) = delete;
    AllocGuard &operator=(AllocGuard &&) = delete;
};

/// True on this thread while an AllocGuard is alive.
[[nodiscard]] bool allocationsForbidden();

/// Called by the hook for every allocation; counts the forbidden ones. Safe to call from any thread.
void noteAllocation();

/// Number of allocations made under a guard since start (or since the last reset).
[[nodiscard]] long long forbiddenAllocationCount();
void resetForbiddenAllocationCount();

} // namespace cc::rt
