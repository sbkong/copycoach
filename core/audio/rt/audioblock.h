// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

// Planar float audio block (coding-standards.md section 18) and the fixed 64-frame storage the engine renders in
// (audio-engine.md section 5). Everything here may run on the audio callback: no allocation (section 8).
namespace cc::rt
{

constexpr int blockFrames = 64; // internal render block (decision 2026-10-26)
constexpr int stereoChannels = 2;

struct AudioBlock {
    std::span<std::span<float>> channels; // L, R
    int frames = 0;

    [[nodiscard]] std::span<float> channel(std::size_t index) const
    {
        return channels[index];
    }

    void clear() const
    {
        for (const std::span<float> ch : channels) {
            std::ranges::fill(ch, 0.0F);
        }
    }

    /// The same block narrowed to frames [offset, offset + count). Spans alias the original storage.
    void sub(int offset, int count, std::span<std::span<float>> target) const
    {
        for (std::size_t i = 0; i < channels.size(); ++i) {
            target[i] = channels[i].subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
        }
    }
};

/// Storage for one stereo block of at most blockFrames frames, allocated once with its owner.
class BlockBuffer
{
public:
    BlockBuffer()
    {
        for (std::size_t i = 0; i < stereoChannels; ++i) {
            m_spans[i] = std::span<float>(m_data[i]);
        }
    }

    /// A block over the first `frames` frames of this buffer, zeroed.
    AudioBlock block(int frames)
    {
        const auto n = static_cast<std::size_t>(std::min(frames, blockFrames));
        for (std::size_t i = 0; i < stereoChannels; ++i) {
            m_spans[i] = std::span<float>(m_data[i]).first(n);
        }
        AudioBlock out{std::span<std::span<float>>(m_spans), static_cast<int>(n)};
        out.clear();
        return out;
    }

private:
    std::array<std::array<float, blockFrames>, stereoChannels> m_data{};
    std::array<std::span<float>, stereoChannels> m_spans{};
};

/// A view of `count` frames of another block, backed by a small span array that lives with this object.
class SubBlock
{
public:
    SubBlock(const AudioBlock &source, int offset, int count)
        : m_block{std::span<std::span<float>>(m_spans).first(source.channels.size()), count}
    {
        source.sub(offset, count, m_spans);
    }

    [[nodiscard]] AudioBlock &block()
    {
        return m_block;
    }

private:
    std::array<std::span<float>, stereoChannels> m_spans{};
    AudioBlock m_block;
};

} // namespace cc::rt
