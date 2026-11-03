// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

// Single-producer single-consumer ring for commands, events, meter values and pre-rendered audio chunks
// (audio-engine.md section 4, coding-standards.md section 8). Fixed capacity, no allocation after construction,
// one slot kept free. Large items (audio chunks) are read in place through front() and released with popFront().
namespace cc::rt
{

template<typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "queue items cross threads by copy; keep them POD");
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    /// Producer side. Returns false when full; the caller keeps the item and retries later (never drops).
    bool push(const T &item)
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask;
        if (next == m_tail.load(std::memory_order_acquire)) {
            return false;
        }
        m_items[head] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false when empty.
    bool pop(T &item)
    {
        const T *slot = front();
        if (slot == nullptr) {
            return false;
        }
        item = *slot;
        popFront();
        return true;
    }

    /// Consumer side: the oldest item, readable in place until popFront(); nullptr when empty.
    [[nodiscard]] const T *front() const
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &m_items[tail];
    }

    /// Consumer side: releases the item front() returned. Only after a non-null front().
    void popFront()
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store((tail + 1) & mask, std::memory_order_release);
    }

    [[nodiscard]] bool empty() const
    {
        return m_tail.load(std::memory_order_acquire) == m_head.load(std::memory_order_acquire);
    }

    /// Items queued right now (either side may call it; the value is a snapshot).
    [[nodiscard]] std::size_t size() const
    {
        const std::size_t head = m_head.load(std::memory_order_acquire);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        return (head - tail) & mask;
    }

private:
    static constexpr std::size_t mask = Capacity - 1;

    std::array<T, Capacity> m_items{};
    std::atomic<std::size_t> m_head{0};
    std::atomic<std::size_t> m_tail{0};
};

} // namespace cc::rt
