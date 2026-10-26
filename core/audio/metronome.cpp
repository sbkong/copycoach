// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cc::audio
{

namespace
{

std::int64_t floorDiv(std::int64_t value, std::int64_t divisor)
{
    const std::int64_t q = value / divisor;
    return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? q - 1 : q;
}

std::int64_t floorMod(std::int64_t value, std::int64_t divisor)
{
    return value - (floorDiv(value, divisor) * divisor);
}

} // namespace

void Metronome::setSet(const MetronomeSet *set)
{
    m_set = set;
    silence();
}

void Metronome::setTimeSignature(int beatsPerMeasure, std::int64_t ticksPerBeat)
{
    m_beatsPerMeasure = std::max(1, beatsPerMeasure);
    m_ticksPerBeat = std::max<std::int64_t>(1, ticksPerBeat);
}

void Metronome::setSubdivision(int perBeat)
{
    m_subdivision = perBeat == 2 || perBeat == 3 ? perBeat : 0;
}

void Metronome::setBeatMask(std::uint32_t mask)
{
    m_beatMask = mask;
}

void Metronome::setPan(float pan)
{
    const float angle = (std::clamp(pan, -1.0F, 1.0F) + 1.0F) * 0.25F * std::numbers::pi_v<float>;
    m_gainLeft = std::cos(angle);
    m_gainRight = std::sin(angle);
}

std::int64_t Metronome::gridTicks() const
{
    return m_subdivision == 0 ? m_ticksPerBeat : m_ticksPerBeat / m_subdivision;
}

void Metronome::process(rt::AudioBlock &block, const rt::TickClock &clock, std::int64_t startSample)
{
    if (m_set != nullptr) {
        schedule(clock, startSample, block.frames);
    }
    mix(block);
}

void Metronome::renderTails(rt::AudioBlock &block)
{
    mix(block);
}

void Metronome::silence()
{
    for (Voice &voice : m_voices) {
        voice.clip = nullptr;
    }
}

/// Walks the grid positions that start inside this block. The first candidate is one step before the tick at
/// the block start so that rounding at the boundary cannot skip a click.
void Metronome::schedule(const rt::TickClock &clock, std::int64_t startSample, int frames)
{
    const std::int64_t grid = gridTicks();
    const std::int64_t endSample = startSample + frames;
    std::int64_t index = floorDiv(clock.tickAt(startSample), grid) - 1;
    for (;; ++index) {
        const std::int64_t sample = clock.sampleAt(index * grid);
        if (sample >= endSample) {
            return;
        }
        if (sample >= startSample) {
            trigger(index, static_cast<int>(sample - startSample));
        }
    }
}

void Metronome::trigger(std::int64_t gridIndex, int offset)
{
    const std::int64_t tick = gridIndex * gridTicks();
    const std::int64_t measureTicks = m_ticksPerBeat * m_beatsPerMeasure;
    const std::int64_t tickInMeasure = floorMod(tick, measureTicks);
    const std::vector<float> *clip = &m_set->sub;
    if (tickInMeasure % m_ticksPerBeat == 0) {
        const auto beatIndex = static_cast<unsigned>(tickInMeasure / m_ticksPerBeat);
        if (beatIndex < 32U && (m_beatMask & (1U << beatIndex)) == 0U) {
            return;
        }
        clip = tickInMeasure == 0 ? &m_set->accent : &m_set->beat;
    }
    if (clip->empty()) {
        return;
    }
    auto slot = std::ranges::find_if(m_voices, [](const Voice &v) {
        return v.clip == nullptr;
    });
    if (slot == m_voices.end()) {
        slot = m_voices.begin(); // steal the oldest-started voice
    }
    *slot = Voice{clip, 0, offset};
}

void Metronome::mix(rt::AudioBlock &block)
{
    const std::span<float> left = block.channel(0);
    const std::span<float> right = block.channel(1);
    for (Voice &voice : m_voices) {
        if (voice.clip != nullptr) {
            renderVoice(voice, left, right);
            voice.startOffset = 0;
        }
    }
}

void Metronome::renderVoice(Voice &voice, std::span<float> left, std::span<float> right) const
{
    for (auto i = static_cast<std::size_t>(voice.startOffset); i < left.size(); ++i) {
        if (voice.position >= voice.clip->size()) {
            voice.clip = nullptr;
            return;
        }
        const float sample = (*voice.clip)[voice.position++];
        left[i] += sample * m_gainLeft;
        right[i] += sample * m_gainRight;
    }
}

} // namespace cc::audio
