// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/sequence.h"

#include <algorithm>

namespace cc::audio
{

NoteSequence makeNoteSequence(const std::vector<routine::NoteEvent> &events, const model::Score &score)
{
    NoteSequence sequence;
    sequence.notes.reserve(events.size());
    for (const routine::NoteEvent &event : events) {
        sequence.notes.push_back(
            SequencedNote{event.onTick, event.offTick, event.pitch, static_cast<int>(event.measureIndex)});
    }
    std::ranges::stable_sort(sequence.notes, {}, &SequencedNote::onTick);
    sequence.slotStartTicks.reserve(score.measures.size());
    for (std::size_t i = 0; i < score.measures.size(); ++i) {
        sequence.slotStartTicks.push_back(score.measureStartTick(i));
    }
    sequence.lengthTicks = score.totalTicks();
    return sequence;
}

void SequencePlayer::setSequence(const NoteSequence *sequence)
{
    m_sequence = sequence;
    m_cursor = 0;
}

void SequencePlayer::setSlotMask(std::uint64_t audibleSlots)
{
    m_audible = audibleSlots;
}

void SequencePlayer::setChannel(int channel)
{
    m_channel = channel;
}

void SequencePlayer::resync(const rt::TickClock &clock, std::int64_t startSample)
{
    if (m_sequence == nullptr) {
        return;
    }
    const std::int64_t tick = clock.tickAt(startSample);
    const auto it = std::ranges::lower_bound(m_sequence->notes, tick, {}, &SequencedNote::onTick);
    m_cursor = static_cast<std::size_t>(it - m_sequence->notes.begin());
    while (m_cursor > 0 && clock.sampleAt(m_sequence->notes[m_cursor - 1].onTick) >= startSample) {
        --m_cursor; // rounding at the boundary: do not skip a note that starts in this block
    }
}

void SequencePlayer::process(ISynth &synth, const rt::TickClock &clock, std::int64_t startSample, int frames)
{
    const std::int64_t endSample = startSample + frames;
    stopDue(synth, endSample);
    if (m_sequence != nullptr) {
        startDue(synth, clock, endSample);
    }
}

void SequencePlayer::startDue(ISynth &synth, const rt::TickClock &clock, std::int64_t endSample)
{
    const std::vector<SequencedNote> &notes = m_sequence->notes;
    while (m_cursor < notes.size() && clock.sampleAt(notes[m_cursor].onTick) < endSample) {
        const SequencedNote &note = notes[m_cursor++];
        const bool audible =
            note.slot >= 64 || (m_audible & (std::uint64_t{1} << static_cast<unsigned>(note.slot))) != 0;
        if (!audible) {
            continue;
        }
        const auto slot = std::ranges::find_if(m_active, [](const Active &a) {
            return a.note < 0;
        });
        if (slot == m_active.end()) {
            continue; // polyphony cap reached: drop rather than allocate
        }
        *slot = Active{note.note, clock.sampleAt(note.offTick)};
        synth.noteOn(m_channel, note.note, velocity);
    }
}

void SequencePlayer::stopDue(ISynth &synth, std::int64_t endSample)
{
    for (Active &active : m_active) {
        if (active.note >= 0 && active.offSample < endSample) {
            synth.noteOff(m_channel, active.note);
            active.note = -1;
        }
    }
}

void SequencePlayer::allNotesOff(ISynth &synth)
{
    for (Active &active : m_active) {
        if (active.note >= 0) {
            synth.noteOff(m_channel, active.note);
            active.note = -1;
        }
    }
}

int SequencePlayer::slotAt(std::int64_t tick) const
{
    if (m_sequence == nullptr || m_sequence->slotStartTicks.empty() || tick < m_sequence->slotStartTicks.front()) {
        return -1;
    }
    const auto it = std::ranges::upper_bound(m_sequence->slotStartTicks, tick);
    return static_cast<int>(it - m_sequence->slotStartTicks.begin()) - 1;
}

} // namespace cc::audio
