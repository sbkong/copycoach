// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/audioblock.h"
#include "core/audio/rt/spscqueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// The pre-rendered file audio on its way from the render worker to the callback (audio-engine.md section 15.2).
// Each chunk carries the source frame it starts at (device-rate frames on the recording's timeline), so the
// callback derives ticks from it and never keeps a second position of its own (section 15.3).
namespace cc::rt
{

struct Chunk {
    std::array<std::array<float, blockFrames>, stereoChannels> samples{};
    std::int64_t sourceFrame = 0;
    std::uint32_t generation = 0; // chunks from before a seek carry an older generation and are dropped
    int frames = 0; // 0 = end of the sources (nothing follows until a seek)
};

struct ChunkRing {
    static constexpr std::size_t capacity = 256; // 341 ms at 48 kHz; the worker fills to 250 ms (section 15.10)

    SpscQueue<Chunk, capacity> queue;
    std::atomic<std::uint32_t> generation{0}; // the worker bumps it on seek; the callback skips older chunks
    std::atomic<std::uint64_t> popped{0}; // every popFront the callback did (played, discarded or end marker)
    std::atomic<std::uint64_t> discarded{0}; // the popped chunks that were stale (older generation)
};

} // namespace cc::rt
