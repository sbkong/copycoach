// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <vector>

// Minimal RIFF/WAVE reader for bundled and user sound sets (audio-engine.md section 7): PCM 16/24-bit and
// IEEE float 32, any channel count. Not a general decoder; music files go through FFmpeg later.
namespace cc::io
{

struct AudioClip {
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels; // planar, full scale 1.0 (coding-standards.md section 18)

    [[nodiscard]] std::size_t frames() const
    {
        return channels.empty() ? 0 : channels.front().size();
    }
};

constexpr std::uintmax_t maxWavFileBytes = std::uintmax_t{64} * 1024 * 1024;

/// FileNotFound when absent; FileCorrupt for anything that is not a PCM16/24 or float32 WAV within the limit.
Result<AudioClip> readWavFile(const std::filesystem::path &path);

/// Average of all channels.
std::vector<float> toMono(const AudioClip &clip);

/// Linear interpolation to `targetRate`; returns the input unchanged when the rates already match.
std::vector<float> resampleLinear(const std::vector<float> &samples, double sourceRate, double targetRate);

} // namespace cc::io
