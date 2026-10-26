// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/routine/sequencer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

namespace cc::routine
{

namespace
{

using model::BeatSpec;
using model::Dur;
using model::MeasureSpec;

/// Lowest string (largest number) first so an arpeggio climbs in pitch.
Voicing lowToHigh(Voicing voicing)
{
    std::ranges::sort(voicing, std::greater<>{}, &theory::StringFret::string);
    return voicing;
}

/// Beat value when the notes do not fit the measure's beats: halve the beat (quarter -> eighth) once.
Dur arpBeatDur(std::size_t noteCount, const model::TimeSignature &timeSig)
{
    const Dur beat = model::beatDur(timeSig);
    if (std::cmp_less_equal(noteCount, timeSig.count) || beat == Dur::SixtyFourth) {
        return beat;
    }
    return static_cast<Dur>(static_cast<int>(beat) + 1);
}

/// The whole voicing on every beat, tied through, so it sounds once and rings for the measure.
MeasureSpec chordMeasure(const Voicing &voicing, const model::TimeSignature &timeSig)
{
    MeasureSpec measure;
    const Voicing notes = lowToHigh(voicing);
    measure.beats.reserve(static_cast<std::size_t>(timeSig.count));
    for (int i = 0; i < timeSig.count; ++i) {
        measure.beats.push_back(BeatSpec{model::beatDur(timeSig), notes, i + 1 < timeSig.count});
    }
    return measure;
}

MeasureSpec arpMeasure(const Voicing &voicing, const model::TimeSignature &timeSig)
{
    MeasureSpec measure;
    const Voicing notes = lowToHigh(voicing);
    const Dur dur = arpBeatDur(notes.size(), timeSig);
    const model::Tick capacity = model::measureTicks(timeSig);
    model::Tick used = 0;
    for (const theory::StringFret &note : notes) {
        if (used + model::baseTicks(dur) > capacity) {
            break; // more notes than even the halved beat holds: drop the rest (one measure per slot, section 1)
        }
        measure.beats.push_back(BeatSpec{dur, {note}, false});
        used += model::baseTicks(dur);
    }
    while (used + model::baseTicks(dur) <= capacity) {
        measure.beats.push_back(BeatSpec{dur, {}, false});
        used += model::baseTicks(dur);
    }
    return measure;
}

/// Tie handling: a note that ties to the next beat extends the open event on its string instead of starting one.
class EventCollector
{
public:
    void
    addBeat(const model::Beat &beat, model::Tick measureStart, std::size_t measureIndex, const theory::Tuning &tuning)
    {
        if (beat.rest) {
            return;
        }
        const model::Tick on = measureStart + beat.tick;
        for (const model::Note &note : beat.notes) {
            add(note, on, on + beat.ticks(), measureIndex, tuning.pitchAt({note.string, note.fret}));
        }
    }

    std::vector<NoteEvent> take()
    {
        std::ranges::stable_sort(m_events, {}, &NoteEvent::onTick);
        return std::move(m_events);
    }

private:
    void add(const model::Note &note, model::Tick on, model::Tick off, std::size_t measureIndex, theory::Midi pitch)
    {
        const auto open = m_openByString.find(note.string);
        if (open != m_openByString.end()) {
            m_events[open->second].offTick = off;
            if (!note.tieToNext) {
                m_openByString.erase(open);
            }
            return;
        }
        m_events.push_back(NoteEvent{on, off, note.string, note.fret, pitch, measureIndex});
        if (note.tieToNext) {
            m_openByString[note.string] = m_events.size() - 1;
        }
    }

    std::vector<NoteEvent> m_events;
    std::map<int, std::size_t> m_openByString;
};

} // namespace

std::vector<MeasureSpec>
slotMeasures(const std::vector<Voicing> &slots, const model::TimeSignature &timeSig, PlaybackStyle style)
{
    std::vector<MeasureSpec> measures;
    measures.reserve(slots.size());
    for (const Voicing &voicing : slots) {
        measures.push_back(style == PlaybackStyle::Chord ? chordMeasure(voicing, timeSig)
                                                         : arpMeasure(voicing, timeSig));
    }
    return measures;
}

std::vector<MeasureSpec> scaleRunMeasures(const Voicing &ascendingForm, const model::TimeSignature &timeSig)
{
    Voicing run = ascendingForm;
    for (auto it = ascendingForm.rbegin() + 1; it != ascendingForm.rend(); ++it) {
        run.push_back(*it);
    }
    const Dur beat = model::beatDur(timeSig);
    const auto perMeasure = static_cast<std::size_t>(timeSig.count);
    std::vector<MeasureSpec> measures;
    measures.reserve((run.size() + perMeasure - 1) / perMeasure);
    for (std::size_t i = 0; i < run.size(); i += perMeasure) {
        MeasureSpec measure;
        for (std::size_t b = 0; b < perMeasure; ++b) {
            const std::size_t index = i + b;
            const Voicing notes = index < run.size() ? Voicing{run[index]} : Voicing{};
            measure.beats.push_back(BeatSpec{beat, notes, false});
        }
        measures.push_back(measure);
    }
    return measures;
}

std::vector<NoteEvent> noteEvents(const model::Score &score)
{
    EventCollector collector;
    for (std::size_t m = 0; m < score.measures.size(); ++m) {
        const model::Tick measureStart = score.measureStartTick(m);
        for (const model::Beat &beat : score.measures[m].beats) {
            collector.addBeat(beat, measureStart, m, score.track.tuning);
        }
    }
    return collector.take();
}

} // namespace cc::routine
