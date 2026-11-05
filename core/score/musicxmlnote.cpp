// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/io/limits.h"
#include "core/score/musicxmlcontext.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string_view>
#include <tuple>

// <note> -> model::Beat / model::Note. The part's cursor (divisions) moves by <duration> unless the note is a
// <chord/> member or a grace note.
namespace cc::score::detail
{

namespace
{

constexpr int maxFallbackFret = 24;

struct NoteRead {
    bool chord = false;
    bool rest = false;
    bool grace = false;
    int duration = 0; // divisions
    int voice = 1;
    int staff = 1;
    std::optional<model::Dur> type;
    int dots = 0;
    std::optional<model::Tuplet> tuplet;
    bool tieStart = false;
    std::optional<theory::StringFret> position;
    std::optional<theory::Midi> midi;
};

std::optional<model::Dur> durOf(std::string_view type)
{
    constexpr std::array<std::pair<std::string_view, model::Dur>, 7> names{{{"whole", model::Dur::Whole},
                                                                            {"half", model::Dur::Half},
                                                                            {"quarter", model::Dur::Quarter},
                                                                            {"eighth", model::Dur::Eighth},
                                                                            {"16th", model::Dur::Sixteenth},
                                                                            {"32nd", model::Dur::ThirtySecond},
                                                                            {"64th", model::Dur::SixtyFourth}}};
    for (const auto &[name, dur] : names) {
        if (name == type) {
            return dur;
        }
    }
    return std::nullopt;
}

/// The plain value closest to a tick length, for notes without a <type>.
model::Dur nearestDur(model::Tick ticks)
{
    constexpr std::array<model::Dur, 7> values{model::Dur::Whole,
                                               model::Dur::Half,
                                               model::Dur::Quarter,
                                               model::Dur::Eighth,
                                               model::Dur::Sixteenth,
                                               model::Dur::ThirtySecond,
                                               model::Dur::SixtyFourth};
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

bool startsHere(const pugi::xml_node &node)
{
    return std::strcmp(node.attribute("type").as_string(), "start") == 0;
}

bool tiedForward(const pugi::xml_node &note)
{
    return std::ranges::any_of(note.children("tie"), startsHere)
        || std::ranges::any_of(note.child("notations").children("tied"), startsHere);
}

NoteRead read(const pugi::xml_node &note)
{
    NoteRead r;
    r.chord = !note.child("chord").empty();
    r.rest = !note.child("rest").empty();
    r.grace = !note.child("grace").empty();
    r.duration = note.child("duration").text().as_int(0);
    r.voice = note.child("voice").text().as_int(1);
    r.staff = note.child("staff").text().as_int(1);
    r.type = durOf(note.child("type").text().as_string());
    r.dots = static_cast<int>(std::distance(note.children("dot").begin(), note.children("dot").end()));
    if (const pugi::xml_node mod = note.child("time-modification"); !mod.empty()) {
        r.tuplet =
            model::Tuplet{mod.child("actual-notes").text().as_int(1), mod.child("normal-notes").text().as_int(1)};
    }
    r.tieStart = tiedForward(note);
    const pugi::xml_node technical = note.child("notations").child("technical");
    if (!technical.child("string").empty() && !technical.child("fret").empty()) {
        r.position =
            theory::StringFret{technical.child("string").text().as_int(0), technical.child("fret").text().as_int(0)};
    }
    r.midi = midiOf(note.child("pitch"));
    return r;
}

/// The lowest fret on any string that sounds the pitch; ties go to the higher-pitched string.
std::optional<theory::StringFret> positionFor(theory::Midi midi, const theory::Tuning &tuning)
{
    std::optional<theory::StringFret> best;
    for (int string = 1; string <= tuning.stringCount(); ++string) {
        const int fret = midi - tuning.pitchAt({string, 0});
        if (fret >= 0 && fret <= maxFallbackFret && (!best || fret < best->fret)) {
            best = theory::StringFret{string, fret};
        }
    }
    return best;
}

bool playable(const theory::StringFret &position, const theory::Tuning &tuning)
{
    return position.string >= 1 && position.string <= tuning.stringCount() && position.fret >= 0
        && position.fret <= maxFret;
}

/// The position to store: as written when it is playable on this tuning, else computed from the pitch.
std::optional<theory::StringFret>
resolvePosition(Importer &in, const NoteRead &r, const theory::Tuning &tuning, std::size_t measureIndex)
{
    if (r.position && playable(*r.position, tuning)) {
        return r.position;
    }
    if (!r.midi) {
        in.warn("measure " + std::to_string(measureIndex + 1)
                + ": note without a playable position or a pitch, dropped");
        return std::nullopt;
    }
    const auto computed = positionFor(*r.midi, tuning);
    if (!computed) {
        in.warn("measure " + std::to_string(measureIndex + 1) + ": pitch " + std::to_string(*r.midi)
                + " is not on this tuning, dropped");
    }
    return computed;
}

int modelVoice(Importer &in, int voice)
{
    const int local = ((voice - 1) % voicesPerStaff) + 1;
    if (local <= 2) {
        return local;
    }
    if (!in.voiceOverflowWarned) {
        in.warn("voices 3 and 4 are merged into voice 2 (the model holds two voices)");
        in.voiceOverflowWarned = true;
    }
    return 2;
}

model::Beat *beatAt(model::Measure &measure, std::size_t track, int voice, model::Tick tick)
{
    for (model::Beat &beat : measure.beats) {
        if (beat.track == track && beat.voice == voice && beat.tick == tick) {
            return &beat;
        }
    }
    return nullptr;
}

/// Where a note lands and the length it brings along if it opens the beat.
struct Slot {
    std::size_t track = 0;
    int voice = 1;
    model::Tick tick = 0;
    model::Dur dur = model::Dur::Quarter;
};

/// The beat at the slot, created from the note's timing when there is none yet; null when the measure is full.
model::Beat *beatFor(Importer &in, model::Measure &measure, const NoteRead &r, const Slot &slot)
{
    if (model::Beat *existing = beatAt(measure, slot.track, slot.voice, slot.tick); existing != nullptr) {
        return existing;
    }
    if (measure.beats.size() >= io::maxBeatsPerMeasure) {
        in.warn("a measure holds more than " + std::to_string(io::maxBeatsPerMeasure) + " beats, the rest is dropped");
        return nullptr;
    }
    model::Beat beat;
    beat.id = in.ids->next();
    beat.track = slot.track;
    beat.voice = slot.voice;
    beat.tick = slot.tick;
    beat.dur = slot.dur;
    beat.dots = r.dots;
    beat.tuplet = r.tuplet;
    beat.rest = r.rest;
    measure.beats.push_back(beat);
    return &measure.beats.back();
}

} // namespace

void readNote(Importer &in, PartState &part, std::size_t measureIndex, const pugi::xml_node &note)
{
    const NoteRead r = read(note);
    const model::Tick tick = Importer::tickOf(part, r.chord ? part.lastStart : part.cursor);
    if (!r.chord && !r.grace) {
        part.lastStart = part.cursor;
        part.cursor += r.duration;
    }
    if (r.grace) {
        in.warn("measure " + std::to_string(measureIndex + 1) + ": grace note dropped");
        return;
    }
    const int sourceStaff = part.tabStaff != 0 ? part.tabStaff : 1;
    if (r.staff != sourceStaff) {
        return; // the notation staff repeats what the TAB staff says
    }
    model::Measure &measure = in.measureAt(measureIndex);
    const theory::Tuning &tuning = in.out.score.tracks[part.track].tuning;
    const int voice = modelVoice(in, r.voice);
    const model::Dur dur = r.type ? *r.type : nearestDur(Importer::tickOf(part, r.duration));
    model::Beat *beat = beatFor(in, measure, r, Slot{part.track, voice, tick, dur});
    if (beat == nullptr) {
        return;
    }
    if (r.rest) {
        return; // a rest beat, or a rest written over notes already there (kept as they are)
    }
    const auto position = resolvePosition(in, r, tuning, measureIndex);
    if (!position || beat->notes.size() >= io::maxNotesPerBeat) {
        return;
    }
    beat->rest = false;
    beat->notes.push_back(model::Note{in.ids->next(), position->string, position->fret, r.tieStart});
}

void sortBeats(model::Measure &measure)
{
    std::ranges::stable_sort(measure.beats, [](const model::Beat &a, const model::Beat &b) {
        return std::tuple(a.track, a.voice, a.tick) < std::tuple(b.track, b.voice, b.tick);
    });
}

} // namespace cc::score::detail
