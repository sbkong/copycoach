// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/rt/tickclock.h"
#include "core/audio/synth.h"
#include "core/routine/sequencer.h"

#include <array>
#include <cstdint>
#include <vector>

// Routine playback (audio-engine.md section 8): the sequencer's tick events become a NoteSequence built off the
// callback; SequencePlayer walks it on the callback and drives the synth. Slots = measures of the routine.
namespace cc::audio
{

struct SequencedNote {
    std::int64_t onTick = 0;
    std::int64_t offTick = 0;
    int note = 0; // MIDI
    int slot = 0; // measure index
};

struct NoteSequence {
    std::vector<SequencedNote> notes; // sorted by onTick
    std::vector<std::int64_t> slotStartTicks; // one per measure, ascending
    std::int64_t lengthTicks = 0; // loop end for "play the whole routine"
};

/// Builds the sequence from the sequencer output and the score it came from (slot boundaries).
NoteSequence makeNoteSequence(const std::vector<routine::NoteEvent> &events, const model::Score &score);

class SequencePlayer
{
public:
    static constexpr int maxActiveNotes = 64;
    static constexpr float velocity = 0.8F;

    void setSequence(const NoteSequence *sequence);
    [[nodiscard]] const NoteSequence *sequence() const
    {
        return m_sequence;
    }
    void setSlotMask(std::uint64_t audibleSlots);
    void setChannel(int channel);

    /// Starts and stops the notes whose on/off samples fall in [startSample, startSample + frames).
    void process(ISynth &synth, const rt::TickClock &clock, std::int64_t startSample, int frames);
    /// Re-aims the cursor after a seek, loop wrap or tempo change (next block starts at `startSample`).
    void resync(const rt::TickClock &clock, std::int64_t startSample);
    void allNotesOff(ISynth &synth);

    /// Slot index at `tick`, or -1 when there is no sequence / before the first slot.
    [[nodiscard]] int slotAt(std::int64_t tick) const;

private:
    struct Active {
        int note = -1;
        std::int64_t offSample = 0;
    };

    void startDue(ISynth &synth, const rt::TickClock &clock, std::int64_t endSample);
    void stopDue(ISynth &synth, std::int64_t endSample);

    const NoteSequence *m_sequence = nullptr;
    std::size_t m_cursor = 0;
    std::uint64_t m_audible = ~std::uint64_t{0};
    int m_channel = 0;
    std::array<Active, maxActiveNotes> m_active{};
};

} // namespace cc::audio
