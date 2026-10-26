// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
//
// Global operator new/delete replacement that reports every allocation to the AllocGuard counter. Built as an
// OBJECT library (cc_allochook) and linked into the test binary and debug app builds only; never into cc_core.
// NOLINTBEGIN(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory, misc-use-anonymous-namespace,
// readability-inconsistent-declaration-parameter-name)
#include "core/audio/rt/allocguard.h"

#include <cstdlib>
#include <new>

namespace
{

void *allocate(std::size_t size)
{
    cc::rt::noteAllocation();
    void *p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

} // namespace

void *operator new(std::size_t size)
{
    return allocate(size);
}

void *operator new[](std::size_t size)
{
    return allocate(size);
}

void *operator new(std::size_t size, const std::nothrow_t & /*tag*/) noexcept
{
    cc::rt::noteAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void *operator new[](std::size_t size, const std::nothrow_t & /*tag*/) noexcept
{
    cc::rt::noteAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void operator delete(void *p) noexcept
{
    std::free(p);
}

void operator delete[](void *p) noexcept
{
    std::free(p);
}

void operator delete(void *p, std::size_t /*size*/) noexcept
{
    std::free(p);
}

void operator delete[](void *p, std::size_t /*size*/) noexcept
{
    std::free(p);
}

void operator delete(void *p, const std::nothrow_t & /*tag*/) noexcept
{
    std::free(p);
}

void operator delete[](void *p, const std::nothrow_t & /*tag*/) noexcept
{
    std::free(p);
}
// NOLINTEND(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory, misc-use-anonymous-namespace,
// readability-inconsistent-declaration-parameter-name)
