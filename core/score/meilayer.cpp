// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/score/meicontext.h"
#include "core/theory/pitch.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>

// Layers of one staff: beats grouped into tuplets and beams, written as notes / chords / rests on the notation
// staff and as tabGrp on the TAB staff. Ties are collected for the measure to append after its staves.
namespace cc::score::detail
{

namespace
{

constexpr std::array<char, 7> letters{'c', 'd', 'e', 'f', 'g', 'a', 'b'};
constexpr std::array<int, 7> naturalPitchClasses{0, 2, 4, 5, 7, 9, 11};
constexpr std::string_view sharpOrder = "fcgdaeb"; // letters sharpened as fifths grow
constexpr std::string_view flatOrder = "beadgcf";

/// What the key signature does to a letter: 's', 'f' or 0.
char keyAlteration(char letter, const model::KeySignature &key)
{
    if (key.fifths > 0 && sharpOrder.substr(0, static_cast<std::size_t>(key.fifths)).contains(letter)) {
        return 's';
    }
    if (key.fifths < 0 && flatOrder.substr(0, static_cast<std::size_t>(-key.fifths)).contains(letter)) {
        return 'f';
    }
    return 0;
}

std::size_t letterIndexOf(int pitchClass)
{
    for (std::size_t i = 0; i < naturalPitchClasses.size(); ++i) {
        if (naturalPitchClasses[i] == pitchClass) {
            return i;
        }
    }
    return 0;
}

} // namespace

/// Sharps in sharp keys, flats in flat keys (and in C), naturals shown only where the key alters the letter.
/// No bar memory: an accidental repeats within a measure, which Verovio draws as written.
Spelling spell(int midi, const model::KeySignature &key)
{
    const int pitchClass = theory::pitchClassOf(midi);
    Spelling s;
    s.octave = (midi / theory::semitonesPerOctave) - 1;
    const bool natural = std::ranges::find(naturalPitchClasses, pitchClass) != naturalPitchClasses.end();
    if (natural) {
        s.pname = letters[letterIndexOf(pitchClass)];
        const char alteration = keyAlteration(s.pname, key);
        s.accid = alteration == 0 ? "" : "n";
        return s;
    }
    const bool useSharp = key.fifths > 0;
    const int base = useSharp ? pitchClass - 1 : pitchClass + 1;
    s.pname = letters[letterIndexOf(theory::wrapPitchClass(base))];
    s.accid = useSharp ? "s" : "f";
    s.inKey = keyAlteration(s.pname, key) == s.accid[0];
    return s;
}

std::string meiDur(model::Dur dur)
{
    constexpr std::array<std::string_view, 7> names{"1", "2", "4", "8", "16", "32", "64"};
    return std::string(names[static_cast<std::size_t>(dur)]);
}

namespace
{

using Beats = std::vector<const model::Beat *>;

void setDuration(pugi::xml_node node, const model::Beat &beat)
{
    node.append_attribute("dur") = meiDur(beat.dur).c_str();
    if (beat.dots > 0) {
        node.append_attribute("dots") = beat.dots;
    }
    node.append_attribute("dur.ppq") = static_cast<long long>(beat.ticks());
}

void writeStaffNote(pugi::xml_node parent, const MeasureContext &ctx, const model::Note &note, std::size_t track)
{
    const theory::Tuning &tuning = ctx.score->tracks[track].tuning;
    const Spelling s = spell(tuning.pitchAt({note.string, note.fret}), ctx.key);
    pugi::xml_node n = parent.append_child("note");
    n.append_attribute("xml:id") = ("ns-" + note.id).c_str();
    n.append_attribute("pname") = std::string(1, s.pname).c_str();
    n.append_attribute("oct") = s.octave;
    if (!s.accid.empty()) {
        n.append_attribute(s.inKey ? "accid.ges" : "accid") = s.accid.c_str();
    }
}

void writeStaffBeat(pugi::xml_node parent, const MeasureContext &ctx, const model::Beat &beat, std::size_t track)
{
    if (beat.rest) {
        pugi::xml_node rest = parent.append_child("rest");
        rest.append_attribute("xml:id") = ("bs-" + beat.id).c_str();
        setDuration(rest, beat);
        return;
    }
    if (beat.notes.size() == 1) {
        writeStaffNote(parent, ctx, beat.notes.front(), track);
        const pugi::xml_node note = parent.last_child();
        setDuration(note, beat);
        return;
    }
    pugi::xml_node chord = parent.append_child("chord");
    chord.append_attribute("xml:id") = ("bs-" + beat.id).c_str();
    setDuration(chord, beat);
    for (const model::Note &note : beat.notes) {
        writeStaffNote(chord, ctx, note, track);
    }
}

/// tabGrp with its duration symbol; a rest is the symbol alone (score-data-path.md section 4).
void writeTabBeat(pugi::xml_node parent, const model::Beat &beat)
{
    pugi::xml_node group = parent.append_child("tabGrp");
    group.append_attribute("xml:id") = ("b-" + beat.id).c_str();
    setDuration(group, beat);
    group.append_child("tabDurSym");
    for (const model::Note &note : beat.notes) {
        pugi::xml_node n = group.append_child("note");
        n.append_attribute("xml:id") = ("n-" + note.id).c_str();
        n.append_attribute("tab.course") = note.string;
        n.append_attribute("tab.fret") = note.fret;
    }
}

bool beamable(const model::Beat &beat)
{
    return !beat.rest && beat.dur >= model::Dur::Eighth;
}

/// Index of the last beat that beams together with beats[i]: beamable and in the same meter beat.
std::size_t beamRunEnd(std::span<const model::Beat *const> beats, std::size_t i, model::Tick beatTicks)
{
    std::size_t end = i;
    if (!beamable(*beats[i])) {
        return end;
    }
    const model::Tick group = beats[i]->tick / beatTicks;
    while (end + 1 < beats.size() && beamable(*beats[end + 1]) && beats[end + 1]->tick / beatTicks == group) {
        ++end;
    }
    return end;
}

/// Consecutive beamable beats inside one meter beat go under <beam>; everything else stands alone.
void writeBeamed(pugi::xml_node parent,
                 const MeasureContext &ctx,
                 std::span<const model::Beat *const> beats,
                 std::size_t track,
                 bool tab)
{
    const model::Tick beatTicks = model::baseTicks(model::beatDur(ctx.meter));
    std::size_t i = 0;
    while (i < beats.size()) {
        const std::size_t end = beamRunEnd(beats, i, beatTicks);
        const pugi::xml_node target = end > i ? parent.append_child("beam") : parent;
        for (std::size_t k = i; k <= end; ++k) {
            tab ? writeTabBeat(target, *beats[k]) : writeStaffBeat(target, ctx, *beats[k], track);
        }
        i = end + 1;
    }
}

bool sameTuplet(const model::Beat &a, const model::Beat &b)
{
    return a.tuplet && b.tuplet && a.tuplet->num == b.tuplet->num && a.tuplet->numbase == b.tuplet->numbase;
}

/// Runs of beats sharing a tuplet ratio go under <tuplet>, beamed inside it.
void writeLayerContent(pugi::xml_node layer, const MeasureContext &ctx, const Beats &beats, std::size_t track, bool tab)
{
    std::size_t i = 0;
    while (i < beats.size()) {
        std::size_t end = i;
        while (beats[i]->tuplet && end + 1 < beats.size() && sameTuplet(*beats[i], *beats[end + 1])) {
            ++end;
        }
        pugi::xml_node target = layer;
        if (const std::optional<model::Tuplet> tuplet = beats[i]->tuplet) {
            target = layer.append_child("tuplet");
            target.append_attribute("num") = tuplet->num;
            target.append_attribute("numbase") = tuplet->numbase;
        }
        writeBeamed(target, ctx, std::span(beats).subspan(i, end - i + 1), track, tab);
        i = end + 1;
    }
}

/// The first beat of the voice after `after` in the measure, or any beat of the voice when `after` is null.
const model::Beat *firstBeatOfVoice(const model::Measure &measure, const model::Beat &like, const model::Beat *after)
{
    for (const model::Beat &beat : measure.beats) {
        if (beat.track == like.track && beat.voice == like.voice && (after == nullptr || beat.tick > after->tick)) {
            return &beat;
        }
    }
    return nullptr;
}

/// The note on the same string of the next beat in this voice (this measure, else the next one).
std::optional<std::string> tieEndId(const MeasureContext &ctx, const model::Beat &from, int string)
{
    const auto &measures = ctx.score->measures;
    const model::Beat *next = firstBeatOfVoice(measures[ctx.measureIndex], from, &from);
    if (next == nullptr && ctx.measureIndex + 1 < measures.size()) {
        next = firstBeatOfVoice(measures[ctx.measureIndex + 1], from, nullptr);
    }
    if (next == nullptr) {
        return std::nullopt;
    }
    const auto note = std::ranges::find(next->notes, string, &model::Note::string);
    if (note == next->notes.end()) {
        return std::nullopt;
    }
    return "ns-" + note->id;
}

void collectTiesOf(MeasureContext &ctx, const model::Beat &beat)
{
    for (const model::Note &note : beat.notes) {
        const auto end = note.tieToNext ? tieEndId(ctx, beat, note.string) : std::nullopt;
        if (end) {
            ctx.ties.emplace_back("ns-" + note.id, *end);
        }
    }
}

void collectTies(MeasureContext &ctx, const Beats &beats)
{
    for (const model::Beat *beat : beats) {
        collectTiesOf(ctx, *beat);
    }
}

Beats beatsOf(const MeasureContext &ctx, std::size_t track, int voice)
{
    Beats beats;
    for (const model::Beat &beat : ctx.score->measures[ctx.measureIndex].beats) {
        if (beat.track == track && beat.voice == voice) {
            beats.push_back(&beat);
        }
    }
    return beats;
}

} // namespace

void writeStaffLayers(MeasureContext &ctx, std::size_t track, int staffNumber, bool tab)
{
    pugi::xml_node staff = ctx.measureNode.append_child("staff");
    staff.append_attribute("n") = staffNumber;
    bool any = false;
    for (int voice = 1; voice <= 2; ++voice) {
        const Beats beats = beatsOf(ctx, track, voice);
        if (beats.empty()) {
            continue;
        }
        any = true;
        pugi::xml_node layer = staff.append_child("layer");
        layer.append_attribute("n") = voice;
        writeLayerContent(layer, ctx, beats, track, tab);
        if (!tab) {
            collectTies(ctx, beats);
        }
    }
    if (!any) {
        staff.append_child("layer").append_attribute("n") = 1;
        staff.first_child().append_child("mRest");
    }
}

} // namespace cc::score::detail
