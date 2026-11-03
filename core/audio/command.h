// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstdint>

// The three ways GUI and audio callback talk (audio-engine.md section 4): commands in, events out, snapshot
// values. All plain data so they cross the SPSC queues by copy.
namespace cc::audio
{

enum class CommandType : std::uint8_t {
    Play, // a = count-in ticks (0 = none); starts at the current position
    Stop, // all notes off, position kept
    Seek, // a = tick
    SetTempo, // value = BPM, from the next block (no ramp)
    SetLoop, // a = start tick, b = end tick; b <= a disables the loop
    SetGain, // a = Source, value = linear gain (ramped over one block)
    SetMute, // a = Source, b = 1 mute / 0 unmute (ramped)
    NoteOn, // a = synth channel, b = MIDI note, value = velocity 0..1 (fretboard / keyboard click)
    NoteOff, // a = synth channel, b = MIDI note
    SwapSequence, // resource = const NoteSequence* (nullptr clears); old pointer comes back in Swapped
    SwapMetronomeSet, // resource = const MetronomeSet*
    SetSlotMask, // a = bit per slot, set = audible (mute / solo resolved by the GUI)
    SetInstrument, // a = synth channel the sequence plays on (0 guitar, 1 piano)
    SetMetronome, // b = 1 on / 0 off (count-in rings regardless, decision 2026-10-26)
    SetSubdivision, // a = 0 none, 2 or 3 per beat
    SetBeatMask, // a = bit per beat of the measure, set = audible
    SetMetronomePan, // value = -1 left .. +1 right
    SetTimeSignature, // a = beats per measure, b = ticks per beat; applied only while stopped
    SwapTimeMap, // resource = const model::TimeMap* (nullptr = routine mode); old pointer comes back in Swapped
    SetFileMode, // b = 1: play the chunk ring (copy), 0: synth/metronome only (routine)
    SetSpeed, // value = playback speed factor (file mode: ticks follow the map tempo times this)
};

enum class Source : std::uint8_t {
    Synth,
    Metronome,
    Input, // software monitoring; off by default (PLAN 4-1)
    File, // the pre-rendered recording mix from the chunk ring (audio-engine.md section 15)
    Count,
};

struct Command {
    CommandType type = CommandType::Stop;
    std::int64_t a = 0;
    std::int64_t b = 0;
    double value = 0.0;
    const void *resource = nullptr;
};

enum class EventType : std::uint8_t {
    LoopWrapped, // a = sample position of the wrap
    CountInDone, // a = sample position where playing started
    SlotChanged, // a = new slot index
    Xrun, // a = backend status flags
    Stopped, // a = position when stopped
    Swapped, // a = CommandType of the swap, resource = the previous pointer (worker frees it)
    FileEnded, // a = position: the sources ran out (no loop); the transport stopped
    FileUnderrun, // a = frames of silence the ring could not supply
};

struct Event {
    EventType type = EventType::Stopped;
    std::int64_t a = 0;
    const void *resource = nullptr;
};

enum class EngineState : std::uint8_t {
    Stopped,
    CountIn,
    Playing,
    Error,
};

struct EngineSnapshot {
    EngineState state = EngineState::Stopped;
    std::int64_t positionSamples = 0;
    std::int64_t positionTicks = 0;
    double sampleRate = 0.0;
    std::uint32_t xruns = 0;
    float inputPeakL = 0.0F;
    float inputPeakR = 0.0F;
    float busPeakL = 0.0F;
    float busPeakR = 0.0F;
    std::int64_t sourceFrame = -1; // file mode: the recording frame now playing (-1 before the first chunk)
    std::uint32_t fileUnderruns = 0;
};

constexpr int guitarChannel = 0; // synth MIDI channel for the guitar sound (audio-engine.md section 2)
constexpr int pianoChannel = 1;

} // namespace cc::audio
