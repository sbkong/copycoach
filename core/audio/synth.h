// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/audioblock.h"

// The synthesizer node seen by the engine (audio-engine.md section 2). FluidSynth implements it in the app;
// tests use a deterministic stand-in. All four calls run on the audio callback: no allocation, no locks.
namespace cc::audio
{

class ISynth
{
public:
    virtual ~ISynth() = default;
    ISynth() = default;
    ISynth(const ISynth &) = delete;
    ISynth &operator=(const ISynth &) = delete;
    ISynth(ISynth &&) = delete;
    ISynth &operator=(ISynth &&) = delete;

    virtual void noteOn(int channel, int note, float velocity) = 0;
    virtual void noteOff(int channel, int note) = 0;
    virtual void allNotesOff() = 0;
    /// Adds its output to the block (the block arrives cleared; FluidSynth also mixes rather than overwrites).
    virtual void process(rt::AudioBlock &block) = 0;
};

} // namespace cc::audio
