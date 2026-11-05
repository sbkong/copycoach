// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/commands.h"
#include "core/edit/measureops.h"
#include "core/edit/snapshot.h"
#include "core/io/limits.h"

#include <algorithm>
#include <cassert>
#include <utility>

// Commands that change one measure's content: notes, beats, chord symbols and the per-measure changes.
namespace cc::edit
{

namespace
{

constexpr double maxTempoBpm = 1000.0; // same bound as the file reader (scorejson)
constexpr double maxSwingRatio = 0.75;
constexpr int maxDots = 2;

std::unique_ptr<Command> replaceMeasure(std::size_t index, model::Measure after, bool timing = false)
{
    std::vector<model::Measure> measures;
    measures.push_back(std::move(after));
    return std::make_unique<MeasureEdit>(index, std::move(measures), timing);
}

model::Measure copyOf(const model::Score &score, std::size_t index)
{
    assert(index < score.measures.size());
    return score.measures[index];
}

/// One optional change on a measure (tempo, meter, key, swing): null when it is already that value.
template<typename T>
std::unique_ptr<Command> setMeasureChange(const model::Score &score,
                                          std::size_t index,
                                          std::optional<T> model::Measure::*field,
                                          std::optional<T> value,
                                          bool timing)
{
    model::Measure measure = copyOf(score, index);
    if (measure.*field == value) {
        return nullptr;
    }
    measure.*field = value;
    return replaceMeasure(index, std::move(measure), timing);
}

bool validTuplet(const std::optional<model::Tuplet> &tuplet)
{
    return !tuplet || (tuplet->num > 0 && tuplet->numbase > 0);
}

} // namespace

std::unique_ptr<Command>
setNote(const model::Score &score, BeatRef at, theory::StringFret note, model::Dur dur, model::IdGenerator &ids)
{
    if (!ops::stringPlayable(score, at.track, note.string) || note.fret < 0 || note.fret > maxFret) {
        return nullptr;
    }
    model::Measure measure = copyOf(score, at.measure);
    if (!ops::putNote(measure, at, note, dur, ids)) {
        return nullptr;
    }
    return replaceMeasure(at.measure, std::move(measure));
}

std::unique_ptr<Command> removeNote(const model::Score &score, BeatRef at, int string)
{
    model::Measure measure = copyOf(score, at.measure);
    const auto beat = ops::beatAt(measure.beats, at);
    if (beat == measure.beats.end()) {
        return nullptr;
    }
    const auto note = std::ranges::find(beat->notes, string, &model::Note::string);
    if (note == beat->notes.end()) {
        return nullptr;
    }
    beat->notes.erase(note);
    beat->rest = beat->notes.empty();
    return replaceMeasure(at.measure, std::move(measure));
}

std::unique_ptr<Command> setRest(const model::Score &score, BeatRef at, model::Dur dur, model::IdGenerator &ids)
{
    model::Measure measure = copyOf(score, at.measure);
    auto beat = ops::beatAt(measure.beats, at);
    if (beat == measure.beats.end()) {
        model::Beat fresh;
        fresh.id = ids.next();
        fresh.track = at.track;
        fresh.voice = at.voice;
        fresh.tick = at.tick;
        beat = measure.beats.insert(ops::slotFor(measure.beats, at), fresh);
    } else if (beat->rest && beat->dur == dur) {
        return nullptr;
    }
    beat->rest = true;
    beat->dur = dur;
    beat->notes.clear();
    return replaceMeasure(at.measure, std::move(measure));
}

std::unique_ptr<Command> deleteBeat(const model::Score &score, BeatRef at)
{
    model::Measure measure = copyOf(score, at.measure);
    const auto beat = ops::beatAt(measure.beats, at);
    if (beat == measure.beats.end()) {
        return nullptr;
    }
    measure.beats.erase(beat);
    return replaceMeasure(at.measure, std::move(measure));
}

std::unique_ptr<Command>
setBeatLength(const model::Score &score, BeatRef at, model::Dur dur, int dots, std::optional<model::Tuplet> tuplet)
{
    if (dots < 0 || dots > maxDots || !validTuplet(tuplet)) {
        return nullptr;
    }
    model::Measure measure = copyOf(score, at.measure);
    const auto beat = ops::beatAt(measure.beats, at);
    if (beat == measure.beats.end()) {
        return nullptr;
    }
    const bool sameTuplet = (!beat->tuplet && !tuplet)
        || (beat->tuplet && tuplet && beat->tuplet->num == tuplet->num && beat->tuplet->numbase == tuplet->numbase);
    if (beat->dur == dur && beat->dots == dots && sameTuplet) {
        return nullptr;
    }
    beat->dur = dur;
    beat->dots = dots;
    beat->tuplet = tuplet;
    return replaceMeasure(at.measure, std::move(measure));
}

std::unique_ptr<Command> setTie(const model::Score &score, BeatRef at, int string, bool tied)
{
    model::Measure measure = copyOf(score, at.measure);
    const auto beat = ops::beatAt(measure.beats, at);
    if (beat == measure.beats.end()) {
        return nullptr;
    }
    const auto note = std::ranges::find(beat->notes, string, &model::Note::string);
    if (note == beat->notes.end() || note->tieToNext == tied) {
        return nullptr;
    }
    note->tieToNext = tied;
    return replaceMeasure(at.measure, std::move(measure));
}

std::unique_ptr<Command> setTempo(const model::Score &score, std::size_t measure, std::optional<double> bpm)
{
    if (bpm && (*bpm <= 0.0 || *bpm > maxTempoBpm)) {
        return nullptr;
    }
    return setMeasureChange(score, measure, &model::Measure::tempoBpm, bpm, true);
}

std::unique_ptr<Command>
setTimeSig(const model::Score &score, std::size_t measure, std::optional<model::TimeSignature> timeSig)
{
    if (timeSig) {
        const bool unitOk =
            timeSig->unit == 1 || timeSig->unit == 2 || timeSig->unit == 4 || timeSig->unit == 8 || timeSig->unit == 16;
        if (timeSig->count < 1 || std::cmp_greater(timeSig->count, io::maxBeatsPerMeasure) || !unitOk) {
            return nullptr;
        }
    }
    return setMeasureChange(score, measure, &model::Measure::timeSig, timeSig, true);
}

std::unique_ptr<Command>
setKeySig(const model::Score &score, std::size_t measure, std::optional<model::KeySignature> keySig)
{
    constexpr int maxFifths = 7;
    if (keySig && (keySig->fifths < -maxFifths || keySig->fifths > maxFifths)) {
        return nullptr;
    }
    return setMeasureChange(score, measure, &model::Measure::keySig, keySig, false);
}

std::unique_ptr<Command> setSwing(const model::Score &score, std::size_t measure, std::optional<double> ratio)
{
    if (ratio && (*ratio < model::straightSwing || *ratio > maxSwingRatio)) {
        return nullptr;
    }
    return setMeasureChange(score, measure, &model::Measure::swingRatio, ratio, false);
}

std::unique_ptr<Command> setChordSymbol(const model::Score &score,
                                        std::size_t measure,
                                        model::Tick tick,
                                        const std::string &text,
                                        model::IdGenerator &ids)
{
    if (text.size() > io::maxNameBytes) {
        return nullptr;
    }
    model::Measure copy = copyOf(score, measure);
    if (!ops::putChord(copy, tick, text, ids)) {
        return nullptr;
    }
    return replaceMeasure(measure, std::move(copy));
}

} // namespace cc::edit
