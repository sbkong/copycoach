// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/synth.h"

#include <array>

// Deterministic stand-in for FluidSynth: every held note adds a constant `level` to both channels, so a test can
// read "how many notes sound at sample N" straight from the output. No allocation after construction.
namespace cc::test
{

class TestSynth final : public audio::ISynth
{
public:
    static constexpr float level = 0.1F;
    static constexpr int maxVoices = 32;

    void noteOn(int channel, int note, float /*velocity*/) override
    {
        for (Voice &v : m_voices) {
            if (!v.on) {
                v = Voice{true, channel, note};
                ++noteOns;
                return;
            }
        }
    }

    void noteOff(int channel, int note) override
    {
        for (Voice &v : m_voices) {
            if (v.on && v.channel == channel && v.note == note) {
                v.on = false;
                ++noteOffs;
                return;
            }
        }
    }

    void allNotesOff() override
    {
        for (Voice &v : m_voices) {
            v.on = false;
        }
    }

    void process(rt::AudioBlock &block) override
    {
        const float sum = static_cast<float>(sounding()) * level;
        for (std::span<float> ch : block.channels) {
            for (float &s : ch) {
                s += sum;
            }
        }
    }

    [[nodiscard]] int sounding() const
    {
        int n = 0;
        for (const Voice &v : m_voices) {
            n += v.on ? 1 : 0;
        }
        return n;
    }

    int noteOns = 0;
    int noteOffs = 0;

private:
    struct Voice {
        bool on = false;
        int channel = 0;
        int note = 0;
    };
    std::array<Voice, maxVoices> m_voices{};
};

} // namespace cc::test
