// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/commands.h"
#include "core/edit/snapshot.h"

#include <algorithm>

// edit.cut / edit.copy / edit.paste over whole measures (ia.md section 2 "편집"): the clipboard holds copies
// of measures; paste replaces the beats and chord symbols of as many measures as the score has from the
// target on, with fresh ids, and keeps the targets' own time signature, key, tempo and swing. Cut is a copy
// followed by clearing. Both are one MeasureEdit each, so undo restores everything in one step.
namespace cc::edit
{

namespace
{

model::Measure cleared(const model::Measure &measure)
{
    model::Measure out = measure;
    out.beats.clear();
    out.chords.clear();
    return out;
}

/// The clip's beats and chords on top of the target's own attributes, every id new; beats of tracks the
/// score lacks are dropped.
model::Measure
pasted(const model::Measure &target, const model::Measure &clip, std::size_t trackCount, model::IdGenerator &ids)
{
    model::Measure out = target;
    out.beats.clear();
    out.chords.clear();
    for (const model::Beat &beat : clip.beats) {
        if (beat.track >= trackCount) {
            continue;
        }
        model::Beat copy = beat;
        copy.id = ids.next();
        for (model::Note &note : copy.notes) {
            note.id = ids.next();
        }
        out.beats.push_back(std::move(copy));
    }
    for (const model::ChordSymbol &chord : clip.chords) {
        model::ChordSymbol copy = chord;
        copy.id = ids.next();
        out.chords.push_back(std::move(copy));
    }
    return out;
}

} // namespace

std::unique_ptr<Command> clearMeasures(const project::Project &project, MeasureSpan measures)
{
    const model::Score &score = project.score;
    if (measures.from > measures.to || measures.to >= score.measures.size()) {
        return nullptr;
    }
    std::vector<model::Measure> after;
    bool changed = false;
    for (std::size_t m = measures.from; m <= measures.to; ++m) {
        changed = changed || !score.measures[m].beats.empty() || !score.measures[m].chords.empty();
        after.push_back(cleared(score.measures[m]));
    }
    if (!changed) {
        return nullptr;
    }
    return std::make_unique<MeasureEdit>(measures.from, std::move(after), false);
}

namespace
{

constexpr int maxFret = 24;

/// The measure with every note moved; false when nothing was there to move.
bool transposed(model::Measure &measure, int semitones)
{
    bool any = false;
    for (model::Beat &beat : measure.beats) {
        std::erase_if(beat.notes, [semitones, &any](model::Note &note) {
            any = true;
            note.fret += semitones;
            return note.fret < 0 || note.fret > maxFret;
        });
        if (!beat.rest && beat.notes.empty()) {
            beat.rest = true;
        }
    }
    return any;
}

} // namespace

std::unique_ptr<Command> transposeMeasures(const project::Project &project, MeasureSpan measures, int semitones)
{
    const model::Score &score = project.score;
    if (semitones == 0 || measures.from > measures.to || measures.to >= score.measures.size()) {
        return nullptr;
    }
    std::vector<model::Measure> after;
    bool changed = false;
    for (std::size_t m = measures.from; m <= measures.to; ++m) {
        after.push_back(score.measures[m]);
        changed = transposed(after.back(), semitones) || changed;
    }
    if (!changed) {
        return nullptr;
    }
    return std::make_unique<MeasureEdit>(measures.from, std::move(after), false);
}

std::unique_ptr<Command> pasteMeasures(const project::Project &project,
                                       std::size_t at,
                                       std::span<const model::Measure> clip,
                                       model::IdGenerator &ids)
{
    const model::Score &score = project.score;
    if (clip.empty() || at >= score.measures.size()) {
        return nullptr;
    }
    const std::size_t count = std::min(clip.size(), score.measures.size() - at);
    std::vector<model::Measure> after;
    after.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        after.push_back(pasted(score.measures[at + i], clip[i], score.tracks.size(), ids));
    }
    return std::make_unique<MeasureEdit>(at, std::move(after), false);
}

} // namespace cc::edit
