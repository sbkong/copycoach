// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <array>
#include <memory>
#include <span>

// Time-stretch and pitch-shift for the render worker (audio-engine.md section 15.2): Signalsmith Stretch 1.1.0
// (MIT, header-only) behind a small wrapper so the template stays out of the worker's header. The time ratio
// is set per call by the input/output lengths; the pitch is a transposition in semitones. Not real-time safe.
namespace cc::audio
{

class Stretcher
{
public:
    static constexpr int channels = 2;
    using Planar = std::array<float *, channels>;
    using ConstPlanar = std::array<const float *, channels>;

    explicit Stretcher(double sampleRate);
    ~Stretcher();
    Stretcher(const Stretcher &) = delete;
    Stretcher &operator=(const Stretcher &) = delete;
    Stretcher(Stretcher &&) = delete;
    Stretcher &operator=(Stretcher &&) = delete;

    void setPitchSemitones(double semitones);
    /// Consumes `inFrames` and produces `outFrames`; the ratio between them is the stretch.
    void process(ConstPlanar input, int inFrames, Planar output, int outFrames);
    void reset();
    [[nodiscard]] int inputLatency() const; // input frames the output lags by
    [[nodiscard]] int outputLatency() const; // plus these output frames

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cc::audio
