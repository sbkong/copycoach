// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/catalog.h"
#include "core/audio/synth.h"
#include "core/error.h"

#include <filesystem>
#include <memory>

// FluidSynth behind the ISynth seam (audio-engine.md section 2, section 7). One instance, channel 0 = guitar
// sound, channel 1 = piano sound. Loading and preset selection happen off the callback; noteOn / noteOff /
// process run on it. The sound font is MuseScore_General.sf3 (decision 2026-10-26).
namespace cc::audio
{

class FluidSynthNode final : public ISynth
{
public:
    static constexpr int polyphony = 64; // audio-engine.md section 5 CPU budget assumption

    explicit FluidSynthNode(double sampleRate);
    ~FluidSynthNode() override;

    /// Loads (or replaces) the sound font. ModelMissing when the file is absent, ModelCorrupt when FluidSynth
    /// rejects it (an SF3 needs a libsndfile-enabled FluidSynth).
    Result<void> loadSoundfont(const std::filesystem::path &path);
    /// Binds a catalog preset (bank, program of the loaded sound font) to a channel.
    Result<void> selectPreset(int channel, const SoundfontPreset &preset);

    void noteOn(int channel, int note, float velocity) override;
    void noteOff(int channel, int note) override;
    void allNotesOff() override;
    void process(rt::AudioBlock &block) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cc::audio
