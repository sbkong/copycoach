// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/audioblock.h"
#include "core/audio/rt/tickclock.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Metronome node (audio-engine.md section 2, section 7): accent / beat / subdivision samples placed on the tick
// grid. The sample set is built by a worker and swapped in by pointer; process() runs on the audio callback.
namespace cc::audio
{

/// One sound set, mono at the engine sample rate (loaded from sounds/metronome/<id>/{accent,beat,sub}.wav).
struct MetronomeSet {
    std::vector<float> accent;
    std::vector<float> beat;
    std::vector<float> sub;
};

class Metronome
{
public:
    static constexpr int maxVoices = 4;
    static constexpr std::uint32_t allBeats = 0xFFFFFFFFU;

    void setSet(const MetronomeSet *set);
    [[nodiscard]] const MetronomeSet *set() const
    {
        return m_set;
    }
    void setTimeSignature(int beatsPerMeasure, std::int64_t ticksPerBeat);
    void setSubdivision(int perBeat); // 0 = none, 2 or 3
    void setBeatMask(std::uint32_t mask);
    void setPan(float pan); // -1 left .. +1 right

    /// Starts the clicks whose grid position falls in [startSample, startSample + block.frames) and mixes all
    /// sounding clicks into the block.
    void process(rt::AudioBlock &block, const rt::TickClock &clock, std::int64_t startSample);
    /// Lets sounding clicks finish but starts no new ones (used while stopped).
    void renderTails(rt::AudioBlock &block);
    void silence();

private:
    struct Voice {
        const std::vector<float> *clip = nullptr;
        std::size_t position = 0;
        int startOffset = 0; // frame inside the current block where the click begins
    };

    void schedule(const rt::TickClock &clock, std::int64_t startSample, int frames);
    void trigger(std::int64_t gridIndex, int offset);
    void mix(rt::AudioBlock &block);
    void renderVoice(Voice &voice, std::span<float> left, std::span<float> right) const;
    [[nodiscard]] std::int64_t gridTicks() const;

    const MetronomeSet *m_set = nullptr;
    int m_beatsPerMeasure = 4;
    std::int64_t m_ticksPerBeat = 960;
    int m_subdivision = 0;
    std::uint32_t m_beatMask = allBeats;
    float m_gainLeft = 0.7071F;
    float m_gainRight = 0.7071F;
    std::array<Voice, maxVoices> m_voices{};
};

} // namespace cc::audio
