// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/engine.h"

#include "core/audio/rt/allocguard.h"
#include "core/audio/units.h"
#include "core/model/timemap.h"

#include <algorithm>
#include <cmath>

namespace cc::audio
{

namespace
{

float peakOf(std::span<const float> samples)
{
    float peak = 0.0F;
    for (const float s : samples) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

/// Adds `source` into `target` with a linear ramp from the current gain to the target gain over this block.
void mixInto(const rt::AudioBlock &target, const rt::AudioBlock &source, GainRamp &ramp)
{
    const float goal = ramp.effectiveTarget();
    const float step = (goal - ramp.current) / static_cast<float>(std::max(1, source.frames));
    for (std::size_t ch = 0; ch < target.channels.size(); ++ch) {
        const std::span<float> out = target.channel(ch);
        const std::span<const float> in = source.channel(ch);
        float gain = ramp.current;
        for (std::size_t i = 0; i < in.size(); ++i) {
            gain += step;
            out[i] += in[i] * gain;
        }
    }
    ramp.current = goal;
}

GainRamp &rampFor(std::array<GainRamp, static_cast<std::size_t>(Source::Count)> &gains, std::int64_t source)
{
    const auto index = static_cast<std::size_t>(std::clamp<std::int64_t>(source, 0, gains.size() - 1));
    return gains[index];
}

/// Copies `count` frames of the chunk from `chunkOffset` into the block at `blockOffset`.
void copyChunk(const rt::Chunk &chunk, int chunkOffset, const rt::AudioBlock &block, int blockOffset, int count)
{
    for (std::size_t ch = 0; ch < block.channels.size(); ++ch) {
        const std::span<const float> from(chunk.samples[ch]);
        std::ranges::copy(from.subspan(static_cast<std::size_t>(chunkOffset), static_cast<std::size_t>(count)),
                          block.channel(ch).begin() + blockOffset);
    }
}

} // namespace

Engine::Engine(double sampleRate, ISynth &synth)
    : m_sampleRate(sampleRate)
    , m_synth(&synth)
    , m_transport(sampleRate, theory::tempoMinBpm)
{
    m_gains[static_cast<std::size_t>(Source::Input)] = GainRamp{0.0F, 0.0F, true}; // monitoring off (PLAN 4-1)
}

bool Engine::post(const Command &command)
{
    return m_commands.push(command);
}

bool Engine::pollEvent(Event &event)
{
    return m_events.pop(event);
}

EngineSnapshot Engine::snapshot() const
{
    EngineSnapshot s;
    s.state = static_cast<EngineState>(m_stateOut.load(std::memory_order_acquire));
    s.positionSamples = m_positionOut.load(std::memory_order_acquire);
    s.positionTicks = m_ticksOut.load(std::memory_order_acquire);
    s.sampleRate = m_sampleRate;
    s.xruns = m_xruns.load(std::memory_order_relaxed);
    s.inputPeakL = m_inputPeakL.load(std::memory_order_relaxed);
    s.inputPeakR = m_inputPeakR.load(std::memory_order_relaxed);
    s.busPeakL = m_busPeakL.load(std::memory_order_relaxed);
    s.busPeakR = m_busPeakR.load(std::memory_order_relaxed);
    s.sourceFrame = m_sourceOut.load(std::memory_order_acquire);
    s.fileUnderruns = m_fileUnderruns.load(std::memory_order_relaxed);
    return s;
}

rt::ChunkRing &Engine::fileRing()
{
    return m_fileRing;
}

// ---- audio thread ------------------------------------------------------------------------------------------

void Engine::process(const rt::AudioBlock &input, rt::AudioBlock &output, std::uint32_t statusFlags)
{
    const rt::AllocGuard guard;
    if (statusFlags != 0U) {
        m_xruns.fetch_add(1, std::memory_order_relaxed);
        publish(EventType::Xrun, statusFlags);
    }
    drainCommands();
    meterInput(input);
    output.clear();
    for (int offset = 0; offset < output.frames; offset += rt::blockFrames) {
        rt::SubBlock sub(output, offset, std::min(rt::blockFrames, output.frames - offset));
        renderBlock(sub.block());
    }
    finishBus(output);
    publishSnapshot();
}

void Engine::drainCommands()
{
    Command command;
    while (m_commands.pop(command)) {
        apply(command);
    }
}

void Engine::apply(const Command &command)
{
    switch (command.type) {
    case CommandType::Play:
    case CommandType::Stop:
    case CommandType::Seek:
    case CommandType::SetTempo:
    case CommandType::SetLoop:
    case CommandType::SetTimeSignature:
        applyTransport(command);
        return;
    case CommandType::SwapSequence:
    case CommandType::SwapMetronomeSet:
    case CommandType::SwapTimeMap:
        applySwap(command);
        return;
    default:
        applySetting(command);
        return;
    }
}

void Engine::applySetting(const Command &command)
{
    switch (command.type) {
    case CommandType::SetGain:
        rampFor(m_gains, command.a).target = static_cast<float>(command.value);
        return;
    case CommandType::SetMute:
        rampFor(m_gains, command.a).muted = command.b != 0;
        return;
    case CommandType::NoteOn:
        m_synth->noteOn(static_cast<int>(command.a), static_cast<int>(command.b), static_cast<float>(command.value));
        return;
    case CommandType::NoteOff:
        m_synth->noteOff(static_cast<int>(command.a), static_cast<int>(command.b));
        return;
    case CommandType::SetSlotMask:
        m_player.setSlotMask(static_cast<std::uint64_t>(command.a));
        return;
    case CommandType::SetInstrument:
        m_player.allNotesOff(*m_synth);
        m_player.setChannel(static_cast<int>(command.a));
        return;
    case CommandType::SetFileMode:
        m_fileMode = command.b != 0;
        m_chunkOffset = 0;
        m_sourceFrame = -1;
        return;
    default:
        applyMetronomeSetting(command);
        return;
    }
}

void Engine::applyMetronomeSetting(const Command &command)
{
    switch (command.type) {
    case CommandType::SetMetronome:
        m_metronomeOn = command.b != 0;
        return;
    case CommandType::SetSubdivision:
        m_metronome.setSubdivision(static_cast<int>(command.a));
        return;
    case CommandType::SetBeatMask:
        m_metronome.setBeatMask(static_cast<std::uint32_t>(command.a));
        return;
    case CommandType::SetMetronomePan:
        m_metronome.setPan(static_cast<float>(command.value));
        return;
    default:
        return;
    }
}

void Engine::applyTransport(const Command &command)
{
    const std::int64_t position = m_transport.positionSamples();
    switch (command.type) {
    case CommandType::Play:
        m_transport.play(command.a);
        break;
    case CommandType::Stop:
        m_transport.stop();
        m_player.allNotesOff(*m_synth);
        publish(EventType::Stopped, position);
        break;
    case CommandType::Seek:
        m_transport.seek(command.a);
        m_player.allNotesOff(*m_synth);
        break;
    case CommandType::SetTempo:
        if (command.value
            > 0.0) { // no clamp: playback speed is the score tempo times a percentage (copy-spec.md section 3)
            m_transport.setTempo(command.value);
        }
        break;
    case CommandType::SetLoop:
        m_transport.setLoop(command.a, command.b);
        break;
    case CommandType::SetTimeSignature:
        if (m_transport.state() == EngineState::Stopped) {
            m_metronome.setTimeSignature(static_cast<int>(command.a), command.b);
        }
        return;
    default:
        return;
    }
    m_player.resync(m_transport.clock(), position);
}

void Engine::applySwap(const Command &command)
{
    const void *previous = nullptr;
    if (command.type == CommandType::SwapTimeMap) {
        previous = m_transport.timeMap();
        m_transport.setTimeMap(static_cast<const model::TimeMap *>(command.resource));
        m_player.resync(m_transport.clock(), m_transport.positionSamples());
    } else if (command.type == CommandType::SwapSequence) {
        previous = m_player.sequence();
        m_player.allNotesOff(*m_synth);
        m_player.setSequence(static_cast<const NoteSequence *>(command.resource));
        m_player.resync(m_transport.clock(), m_transport.positionSamples());
    } else {
        previous = m_metronome.set();
        m_metronome.setSet(static_cast<const MetronomeSet *>(command.resource));
    }
    publish(EventType::Swapped, static_cast<std::int64_t>(command.type), previous);
}

/// Renders one block, splitting it wherever the transport crosses a loop end or the count-in end. A boundary
/// reached exactly at the end of the block is crossed now, so the position never rests on the loop end.
void Engine::renderBlock(rt::AudioBlock &output)
{
    int done = 0;
    for (;;) {
        if (m_transport.boundaryDue()) {
            crossBoundary();
            continue;
        }
        if (done >= output.frames) {
            break;
        }
        const int frames = m_transport.framesUntilBoundary(output.frames - done);
        renderSpan(output, done, frames);
        done += frames;
    }
    publishSlot();
}

void Engine::renderSpan(rt::AudioBlock &output, int offset, int frames)
{
    const std::int64_t start = m_transport.positionSamples();
    const EngineState state = m_transport.state();

    const rt::AudioBlock fileBlock = m_fileBuffer.block(frames);
    if (m_fileMode && state == EngineState::Playing) {
        consumeFile(fileBlock); // first: a chunk may re-aim the clock the player reads next
    }
    rt::AudioBlock synthBlock = m_synthBuffer.block(frames);
    if (state == EngineState::Playing) {
        m_player.process(*m_synth, m_transport.clock(), start, frames);
    }
    m_synth->process(synthBlock);

    rt::AudioBlock clickBlock = m_clickBuffer.block(frames);
    if (state == EngineState::CountIn || (state == EngineState::Playing && m_metronomeOn)) {
        m_metronome.process(clickBlock, m_transport.clock(), start);
    } else {
        m_metronome.renderTails(clickBlock);
    }

    rt::SubBlock target(output, offset, frames);
    mixInto(target.block(), synthBlock, m_gains[static_cast<std::size_t>(Source::Synth)]);
    mixInto(target.block(), clickBlock, m_gains[static_cast<std::size_t>(Source::Metronome)]);
    mixInto(target.block(), fileBlock, m_gains[static_cast<std::size_t>(Source::File)]);
    m_transport.advance(frames);
}

/// Copies `block.frames` frames out of the ring into the (zeroed) block. Chunk boundaries fall anywhere inside
/// the block; a ring that runs dry leaves silence and counts an underrun, the source position does not move.
void Engine::consumeFile(const rt::AudioBlock &block)
{
    int done = 0;
    while (done < block.frames) {
        const rt::Chunk *chunk = currentChunk();
        if (chunk == nullptr) {
            noteUnderrun(block.frames - done);
            return;
        }
        const int count = std::min(chunk->frames - m_chunkOffset, block.frames - done);
        copyChunk(*chunk, m_chunkOffset, block, done, count);
        m_chunkOffset += count;
        m_sourceFrame += count;
        done += count;
        if (m_chunkOffset == chunk->frames) {
            m_fileRing.queue.popFront();
            m_fileRing.popped.fetch_add(1, std::memory_order_relaxed);
            m_chunkOffset = 0;
        }
    }
}

/// The chunk to play from now: skips chunks of an older generation (before a seek), handles the end marker,
/// and aligns the clock when a new chunk begins. nullptr when the ring is empty or the sources ended.
const rt::Chunk *Engine::currentChunk()
{
    const std::uint32_t generation = m_fileRing.generation.load(std::memory_order_relaxed);
    for (;;) {
        const rt::Chunk *chunk = m_fileRing.queue.front();
        if (chunk == nullptr) {
            return nullptr;
        }
        if (chunk->generation != generation) {
            m_fileRing.queue.popFront();
            m_fileRing.popped.fetch_add(1, std::memory_order_relaxed);
            m_fileRing.discarded.fetch_add(1, std::memory_order_relaxed);
            m_chunkOffset = 0;
            continue;
        }
        if (chunk->frames == 0) {
            m_fileRing.queue.popFront();
            m_fileRing.popped.fetch_add(1, std::memory_order_relaxed);
            endFile();
            return nullptr;
        }
        if (m_chunkOffset == 0) {
            alignToChunk(*chunk);
        }
        return chunk;
    }
}

void Engine::alignToChunk(const rt::Chunk &chunk)
{
    const bool known = m_sourceFrame >= 0;
    const bool jumpedBack = known && chunk.sourceFrame < m_sourceFrame;
    const bool jumped = known && chunk.sourceFrame != m_sourceFrame;
    m_sourceFrame = chunk.sourceFrame;
    const bool clockChanged = m_transport.syncToSource(chunk.sourceFrame);
    if (jumped || clockChanged) {
        if (jumped) {
            m_player.allNotesOff(*m_synth);
        }
        m_player.resync(m_transport.clock(), m_transport.positionSamples());
    }
    if (jumpedBack) {
        publish(EventType::LoopWrapped, m_transport.positionSamples());
    }
}

/// The ring ran dry while playing: silence for the rest of the block, counted and reported (not after the end).
void Engine::noteUnderrun(int frames)
{
    if (m_transport.state() == EngineState::Playing) {
        m_fileUnderruns.fetch_add(1, std::memory_order_relaxed);
        publish(EventType::FileUnderrun, frames);
    }
}

void Engine::endFile()
{
    const std::int64_t position = m_transport.positionSamples();
    m_transport.stop();
    m_player.allNotesOff(*m_synth);
    m_chunkOffset = 0;
    publish(EventType::FileEnded, position);
    publish(EventType::Stopped, position);
}

void Engine::crossBoundary()
{
    const std::int64_t position = m_transport.positionSamples();
    switch (m_transport.crossBoundary()) {
    case rt::Boundary::LoopWrapped:
        m_player.allNotesOff(*m_synth);
        m_player.resync(m_transport.clock(), position);
        publish(EventType::LoopWrapped, position);
        break;
    case rt::Boundary::CountInDone:
        m_player.resync(m_transport.clock(), position);
        publish(EventType::CountInDone, position);
        break;
    case rt::Boundary::None:
        break;
    }
}

void Engine::finishBus(rt::AudioBlock &output)
{
    for (std::span<float> ch : output.channels) {
        for (float &s : ch) {
            s = softClip(s);
        }
    }
    m_busPeakL.store(peakOf(output.channel(0)), std::memory_order_relaxed);
    m_busPeakR.store(peakOf(output.channel(1)), std::memory_order_relaxed);
}

void Engine::meterInput(const rt::AudioBlock &input)
{
    const float left = input.channels.empty() ? 0.0F : peakOf(input.channel(0));
    const float right = input.channels.size() > 1 ? peakOf(input.channel(1)) : left;
    m_inputPeakL.store(left, std::memory_order_relaxed);
    m_inputPeakR.store(right, std::memory_order_relaxed);
}

void Engine::publishSlot()
{
    if (m_transport.state() != EngineState::Playing) {
        return;
    }
    const int slot = m_player.slotAt(m_transport.positionTicks());
    if (slot != m_lastSlot) {
        m_lastSlot = slot;
        publish(EventType::SlotChanged, slot);
    }
}

void Engine::publishSnapshot()
{
    m_stateOut.store(static_cast<std::uint8_t>(m_transport.state()), std::memory_order_release);
    m_positionOut.store(m_transport.positionSamples(), std::memory_order_release);
    m_ticksOut.store(m_transport.positionTicks(), std::memory_order_release);
    m_sourceOut.store(m_fileMode ? m_sourceFrame : -1, std::memory_order_release);
}

void Engine::publish(EventType type, std::int64_t a, const void *resource)
{
    static_cast<void>(m_events.push(Event{type, a, resource})); // a full event queue drops the oldest news only
}

} // namespace cc::audio
