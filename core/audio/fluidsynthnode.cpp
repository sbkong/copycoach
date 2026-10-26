// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/fluidsynthnode.h"

#include <fluidsynth.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace cc::audio
{

struct FluidSynthNode::Impl {
    fluid_settings_t *settings = nullptr;
    fluid_synth_t *synth = nullptr;
    int soundfontId = FLUID_FAILED;

    static fluid_settings_t *makeSettings(double sampleRate)
    {
        fluid_settings_t *settings = new_fluid_settings();
        fluid_settings_setnum(settings, "synth.sample-rate", sampleRate);
        fluid_settings_setint(settings, "synth.polyphony", polyphony);
        fluid_settings_setint(settings, "synth.reverb.active", 0);
        fluid_settings_setint(settings, "synth.chorus.active", 0);
        fluid_settings_setint(settings, "synth.threadsafe-api", 0); // commands reach us on the audio thread only
        fluid_settings_setint(settings, "synth.audio-channels", 1);
        fluid_settings_setnum(settings, "synth.gain", 0.5);
        return settings;
    }

    explicit Impl(double sampleRate)
        : settings(makeSettings(sampleRate))
        , synth(new_fluid_synth(settings))
    {
    }

    ~Impl()
    {
        if (synth != nullptr) {
            delete_fluid_synth(synth);
        }
        if (settings != nullptr) {
            delete_fluid_settings(settings);
        }
    }

    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;
};

FluidSynthNode::FluidSynthNode(double sampleRate)
    : m_impl(std::make_unique<Impl>(sampleRate))
{
}

FluidSynthNode::~FluidSynthNode() = default;

Result<void> FluidSynthNode::loadSoundfont(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path)) {
        return fail(ErrorCode::ModelMissing, path.string());
    }
    const int id = fluid_synth_sfload(m_impl->synth, path.string().c_str(), 1);
    if (id == FLUID_FAILED) {
        return fail(ErrorCode::ModelCorrupt, path.string(), "FluidSynth could not load the sound font");
    }
    if (m_impl->soundfontId != FLUID_FAILED) {
        fluid_synth_sfunload(m_impl->synth, m_impl->soundfontId, 1);
    }
    m_impl->soundfontId = id;
    return {};
}

Result<void> FluidSynthNode::selectPreset(int channel, const SoundfontPreset &preset)
{
    if (m_impl->soundfontId == FLUID_FAILED) {
        return fail(ErrorCode::ModelMissing, preset.soundfont, "no sound font loaded");
    }
    const int ok = fluid_synth_program_select(m_impl->synth, channel, m_impl->soundfontId, preset.bank, preset.program);
    if (ok == FLUID_FAILED) {
        return fail(ErrorCode::ModelCorrupt,
                    preset.soundfont,
                    "preset " + std::to_string(preset.bank) + ":" + std::to_string(preset.program) + " not found");
    }
    return {};
}

void FluidSynthNode::noteOn(int channel, int note, float velocity)
{
    const int midiVelocity = std::clamp(static_cast<int>(std::lround(velocity * 127.0F)), 1, 127);
    fluid_synth_noteon(m_impl->synth, channel, note, midiVelocity);
}

void FluidSynthNode::noteOff(int channel, int note)
{
    fluid_synth_noteoff(m_impl->synth, channel, note);
}

void FluidSynthNode::allNotesOff()
{
    fluid_synth_all_notes_off(m_impl->synth, -1);
}

void FluidSynthNode::process(rt::AudioBlock &block)
{
    // fluid_synth_process mixes into the buffers (does not clear them), which is what ISynth asks for.
    std::array<float *, 2> out{block.channel(0).data(), block.channel(1).data()};
    fluid_synth_process(m_impl->synth, block.frames, 0, nullptr, static_cast<int>(out.size()), out.data());
}

} // namespace cc::audio
