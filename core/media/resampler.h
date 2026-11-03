// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <cstdint>
#include <span>

struct SRC_STATE_tag;

// Streaming sample-rate conversion through libsamplerate (audio-engine.md section 15.2): the render worker
// turns a file's rate (44.1 k stem caches, anything a user imports) into the device rate as it reads.
// Sinc, medium quality. Not for the audio callback.
namespace cc::media
{

class Resampler
{
public:
    explicit Resampler(int channels);
    ~Resampler();
    Resampler(const Resampler &) = delete;
    Resampler &operator=(const Resampler &) = delete;
    Resampler(Resampler &&) = delete;
    Resampler &operator=(Resampler &&) = delete;

    struct Progress {
        std::int64_t inputUsed = 0; // frames consumed from `input`
        std::int64_t outputProduced = 0; // frames written to `output`
    };

    /// `ratio` = output rate / input rate. Interleaved frames in and out. With `endOfInput` the tail is flushed,
    /// so keep calling until outputProduced is 0.
    [[nodiscard]] Result<Progress>
    process(std::span<const float> input, std::span<float> output, double ratio, bool endOfInput);
    /// Forgets the history (after a seek).
    void reset();
    [[nodiscard]] int channels() const;

private:
    SRC_STATE_tag *m_state = nullptr;
    int m_channels;
};

} // namespace cc::media
