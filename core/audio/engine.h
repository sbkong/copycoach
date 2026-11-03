// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/backend.h"
#include "core/audio/command.h"
#include "core/audio/metronome.h"
#include "core/audio/rt/audioblock.h"
#include "core/audio/rt/chunkring.h"
#include "core/audio/rt/spscqueue.h"
#include "core/audio/rt/transport.h"
#include "core/audio/sequence.h"
#include "core/audio/synth.h"

#include <array>
#include <atomic>
#include <cstdint>

// The audio engine (audio-engine.md sections 2 to 5, 15): a fixed pull graph rendered in 64-frame blocks on the
// backend callback. GUI side: post() commands, pollEvent(), snapshot(). Callback side: process().
// Graph: Synth + Metronome + File (the chunk ring the render worker fills) -> monitor bus -> soft clip -> out,
// input -> meter only (monitoring off). In file mode the recording is the clock master (section 15.3).
namespace cc::audio
{

/// One-block linear gain ramp (click-free gain and mute changes, section 4).
struct GainRamp {
    float current = 1.0F;
    float target = 1.0F;
    bool muted = false;

    [[nodiscard]] float effectiveTarget() const
    {
        return muted ? 0.0F : target;
    }
};

class Engine final : public IAudioProcessor
{
public:
    static constexpr std::size_t queueCapacity = 256;

    Engine(double sampleRate, ISynth &synth);

    // GUI thread
    [[nodiscard]] bool post(const Command &command);
    [[nodiscard]] bool pollEvent(Event &event);
    [[nodiscard]] EngineSnapshot snapshot() const;
    /// The ring the render worker fills (producer side); the callback consumes it in file mode.
    [[nodiscard]] rt::ChunkRing &fileRing();

    // audio thread
    void process(const rt::AudioBlock &input, rt::AudioBlock &output, std::uint32_t statusFlags) override;

private:
    void drainCommands();
    void apply(const Command &command);
    void applyTransport(const Command &command);
    void applySwap(const Command &command);
    void applySetting(const Command &command);
    void applyMetronomeSetting(const Command &command);
    void renderBlock(rt::AudioBlock &output);
    void renderSpan(rt::AudioBlock &output, int offset, int frames);
    void consumeFile(const rt::AudioBlock &block);
    [[nodiscard]] const rt::Chunk *currentChunk();
    void alignToChunk(const rt::Chunk &chunk);
    void noteUnderrun(int frames);
    void endFile();
    void crossBoundary();
    void finishBus(rt::AudioBlock &output);
    void meterInput(const rt::AudioBlock &input);
    void publishSlot();
    void publishSnapshot();
    void publish(EventType type, std::int64_t a, const void *resource = nullptr);

    double m_sampleRate;
    ISynth *m_synth;
    rt::Transport m_transport;
    Metronome m_metronome;
    SequencePlayer m_player;
    std::array<GainRamp, static_cast<std::size_t>(Source::Count)> m_gains{};
    bool m_metronomeOn = false;
    int m_lastSlot = -1;

    rt::SpscQueue<Command, queueCapacity> m_commands;
    rt::SpscQueue<Event, queueCapacity> m_events;
    rt::BlockBuffer m_synthBuffer;
    rt::BlockBuffer m_clickBuffer;
    rt::BlockBuffer m_fileBuffer;

    rt::ChunkRing m_fileRing;
    bool m_fileMode = false;
    int m_chunkOffset = 0; // frames of the front chunk already played
    std::int64_t m_sourceFrame = -1; // recording frame of the next file sample; -1 before the first chunk

    std::atomic<std::uint8_t> m_stateOut{0};
    std::atomic<std::int64_t> m_positionOut{0};
    std::atomic<std::int64_t> m_ticksOut{0};
    std::atomic<std::int64_t> m_sourceOut{-1};
    std::atomic<std::uint32_t> m_xruns{0};
    std::atomic<std::uint32_t> m_fileUnderruns{0};
    std::atomic<float> m_inputPeakL{0.0F};
    std::atomic<float> m_inputPeakR{0.0F};
    std::atomic<float> m_busPeakL{0.0F};
    std::atomic<float> m_busPeakR{0.0F};
};

} // namespace cc::audio
