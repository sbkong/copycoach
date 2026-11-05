// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/measureops.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <tuple>

namespace cc::edit::ops
{

namespace
{

auto orderKey(const model::Beat &beat)
{
    return std::tuple(beat.track, beat.voice, beat.tick);
}

} // namespace

std::vector<model::Beat>::iterator slotFor(std::vector<model::Beat> &beats, const BeatRef &at)
{
    const auto key = std::tuple(at.track, at.voice, at.tick);
    return std::ranges::find_if(beats, [&key](const model::Beat &beat) {
        return orderKey(beat) >= key;
    });
}

std::vector<model::Beat>::iterator beatAt(std::vector<model::Beat> &beats, const BeatRef &at)
{
    auto it = slotFor(beats, at);
    if (it == beats.end() || orderKey(*it) != std::tuple(at.track, at.voice, at.tick)) {
        return beats.end();
    }
    return it;
}

bool stringPlayable(const model::Score &score, std::size_t track, int string)
{
    return track < score.tracks.size() && string >= 1
        && static_cast<std::size_t>(string) <= score.tracks[track].tuning.strings.size();
}

bool putNote(model::Measure &measure,
             const BeatRef &at,
             theory::StringFret note,
             model::Dur dur,
             model::IdGenerator &ids)
{
    auto beat = beatAt(measure.beats, at);
    if (beat == measure.beats.end()) {
        model::Beat fresh;
        fresh.id = ids.next();
        fresh.track = at.track;
        fresh.voice = at.voice;
        fresh.tick = at.tick;
        fresh.dur = dur;
        beat = measure.beats.insert(slotFor(measure.beats, at), fresh);
    }
    const auto existing = std::ranges::find(beat->notes, note.string, &model::Note::string);
    if (existing == beat->notes.end()) {
        beat->notes.push_back(model::Note{ids.next(), note.string, note.fret, false});
    } else if (existing->fret == note.fret && !beat->rest) {
        return false;
    } else {
        existing->fret = note.fret;
    }
    beat->rest = false;
    return true;
}

bool putChord(model::Measure &measure, model::Tick tick, const std::string &text, model::IdGenerator &ids)
{
    auto &chords = measure.chords;
    const auto it = std::ranges::find_if(chords, [tick](const model::ChordSymbol &chord) {
        return chord.tick >= tick;
    });
    const bool exists = it != chords.end() && it->tick == tick;
    if (text.empty()) {
        if (!exists) {
            return false;
        }
        chords.erase(it);
        return true;
    }
    if (exists) {
        if (it->text == text) {
            return false;
        }
        it->text = text;
        return true;
    }
    chords.insert(it, model::ChordSymbol{ids.next(), tick, text});
    return true;
}

model::Dur nearestDur(model::Tick ticks)
{
    constexpr std::array<model::Dur, 7> values{model::Dur::Whole,
                                               model::Dur::Half,
                                               model::Dur::Quarter,
                                               model::Dur::Eighth,
                                               model::Dur::Sixteenth,
                                               model::Dur::ThirtySecond,
                                               model::Dur::SixtyFourth};
    if (ticks <= 0) {
        return model::Dur::Quarter;
    }
    model::Dur best = model::Dur::Quarter;
    model::Tick bestDistance = -1;
    for (const model::Dur dur : values) {
        const model::Tick distance = std::llabs(model::baseTicks(dur) - ticks);
        if (bestDistance < 0 || distance < bestDistance) {
            best = dur;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace cc::edit::ops
