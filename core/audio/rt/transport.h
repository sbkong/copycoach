// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/command.h"
#include "core/audio/rt/tickclock.h"

#include <cstdint>

namespace cc::model
{
class TimeMap;
}

// Transport state machine and the sample clock (audio-engine.md section 3). Runs on the audio callback.
// The engine asks how far it may render before a boundary (loop end, count-in end), renders, then advances;
// crossing a boundary is reported so the engine can emit the event.
// With a TimeMap attached (file playback, section 15.3) the recording is the master: every chunk's source frame
// re-aims the tick clock through syncToSource(), so ticks follow the sync points and the score tempo after them.
namespace cc::rt
{

enum class Boundary : std::uint8_t {
    None,
    LoopWrapped,
    CountInDone,
};

class Transport
{
public:
    Transport(double sampleRate, double bpm);

    void play(std::int64_t countInTicks);
    void stop();
    void seek(std::int64_t tick);
    void setLoop(std::int64_t startTick, std::int64_t endTick);
    void setTempo(double bpm);

    /// nullptr = routine mode (constant tempo segments set by setTempo). The map is owned by the GUI side,
    /// immutable, swapped like any resource.
    void setTimeMap(const model::TimeMap *map);
    [[nodiscard]] const model::TimeMap *timeMap() const;
    /// Playback speed factor in file mode: ticks advance at the map tempo times this (section 15.3).
    void setSpeed(double factor);
    /// Declares that the audio about to play at the current position is source frame `sourceFrame`
    /// (device-rate frames of the recording). Re-aims the clock and tempo from the map. Returns true when the
    /// clock changed, so the engine re-syncs the sequence player.
    bool syncToSource(std::int64_t sourceFrame);

    /// Frames that can be rendered before the next boundary, at most `frames`. 0 means a boundary is due now.
    [[nodiscard]] int framesUntilBoundary(int frames) const;
    /// Moves the clock by `frames` (only while counting in or playing).
    void advance(int frames);
    /// True while running and the position has reached the next boundary.
    [[nodiscard]] bool boundaryDue() const;
    /// Handles the boundary that is due at the current position and says which one it was.
    Boundary crossBoundary();

    [[nodiscard]] audio::EngineState state() const;
    [[nodiscard]] std::int64_t positionSamples() const;
    [[nodiscard]] std::int64_t positionTicks() const;
    [[nodiscard]] const TickClock &clock() const;
    [[nodiscard]] bool loopEnabled() const;
    [[nodiscard]] bool running() const;

private:
    [[nodiscard]] std::int64_t boundarySample() const;

    TickClock m_clock;
    const model::TimeMap *m_map = nullptr;
    double m_speed = 1.0;
    std::int64_t m_lastSource = 0; // the source frame of the last sync, for a speed change between chunks
    audio::EngineState m_state = audio::EngineState::Stopped;
    std::int64_t m_position = 0;
    std::int64_t m_resumeTick = 0; // where count-in ends
    std::int64_t m_loopStart = 0;
    std::int64_t m_loopEnd = 0; // <= start: no loop
};

} // namespace cc::rt
