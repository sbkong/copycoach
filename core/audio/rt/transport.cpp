// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/rt/transport.h"

#include "core/model/timemap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cc::rt
{

using audio::EngineState;

Transport::Transport(double sampleRate, double bpm)
    : m_clock(sampleRate, bpm)
{
}

void Transport::play(std::int64_t countInTicks)
{
    if (m_state != EngineState::Stopped) {
        return;
    }
    const std::int64_t tick = m_clock.tickAt(m_position);
    if (countInTicks > 0) {
        m_resumeTick = tick;
        m_clock.rebase(m_position, tick - countInTicks);
        m_state = EngineState::CountIn;
        return;
    }
    m_state = EngineState::Playing;
}

void Transport::stop()
{
    if (m_state == EngineState::CountIn) {
        m_clock.rebase(m_position, m_resumeTick); // count-in never happened: stay where play was pressed
    }
    m_state = EngineState::Stopped;
}

void Transport::seek(std::int64_t tick)
{
    if (m_state == EngineState::CountIn) {
        m_state = EngineState::Playing;
    }
    m_clock.rebase(m_position, tick);
}

void Transport::setLoop(std::int64_t startTick, std::int64_t endTick)
{
    m_loopStart = startTick;
    m_loopEnd = endTick;
}

void Transport::setTempo(double bpm)
{
    m_clock.setTempoAt(m_position, bpm);
}

void Transport::setTimeMap(const model::TimeMap *map)
{
    m_map = map;
}

const model::TimeMap *Transport::timeMap() const
{
    return m_map;
}

bool Transport::syncToSource(std::int64_t sourceFrame)
{
    if (m_map == nullptr) {
        return false;
    }
    const std::int64_t tick = m_map->sampleToTick(sourceFrame);
    const double bpm = m_map->segmentAtSample(sourceFrame).bpm;
    bool changed = false;
    if (std::abs(m_clock.sampleAt(tick) - m_position)
        > std::llround(m_clock.samplesPerTick())) { // drifted a tick: re-aim
        m_clock.rebase(m_position, tick);
        changed = true;
    }
    if (bpm != m_clock.bpm()) {
        m_clock.setTempoAt(m_position, bpm);
        changed = true;
    }
    return changed;
}

std::int64_t Transport::boundarySample() const
{
    switch (m_state) {
    case EngineState::CountIn:
        return m_clock.sampleAt(m_resumeTick);
    case EngineState::Playing:
        return loopEnabled() ? m_clock.sampleAt(m_loopEnd) : std::numeric_limits<std::int64_t>::max();
    default:
        return std::numeric_limits<std::int64_t>::max();
    }
}

int Transport::framesUntilBoundary(int frames) const
{
    if (!running()) {
        return frames;
    }
    const std::int64_t remaining = boundarySample() - m_position;
    if (remaining <= 0) {
        return 0;
    }
    return static_cast<int>(std::min<std::int64_t>(remaining, frames));
}

void Transport::advance(int frames)
{
    if (running()) {
        m_position += frames;
    }
}

bool Transport::boundaryDue() const
{
    return running() && m_position >= boundarySample();
}

Boundary Transport::crossBoundary()
{
    if (!boundaryDue()) {
        return Boundary::None;
    }
    if (m_state == EngineState::CountIn) {
        m_state = EngineState::Playing;
        return Boundary::CountInDone;
    }
    m_clock.rebase(m_position, m_loopStart);
    return Boundary::LoopWrapped;
}

EngineState Transport::state() const
{
    return m_state;
}

std::int64_t Transport::positionSamples() const
{
    return m_position;
}

std::int64_t Transport::positionTicks() const
{
    return m_clock.tickAt(m_position);
}

const TickClock &Transport::clock() const
{
    return m_clock;
}

bool Transport::loopEnabled() const
{
    return m_loopEnd > m_loopStart;
}

bool Transport::running() const
{
    return m_state == EngineState::CountIn || m_state == EngineState::Playing;
}

} // namespace cc::rt
