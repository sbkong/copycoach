// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/project/scorejson.h"

#include "core/io/limits.h"
#include "core/project/jsonread.h"
#include "core/theory/constants.h"

#include <array>
#include <utility>

namespace cc::project
{

namespace
{

using jsonread::arrayAt;
using jsonread::boolAt;
using jsonread::Corrupt;
using jsonread::idAt;
using jsonread::intAt;
using jsonread::nameAt;
using jsonread::numberAt;
using jsonread::strAt;
using nlohmann::json;

constexpr std::array<std::pair<model::Dur, std::string_view>, 7> durNames{{
    {model::Dur::Whole, "whole"},
    {model::Dur::Half, "half"},
    {model::Dur::Quarter, "quarter"},
    {model::Dur::Eighth, "eighth"},
    {model::Dur::Sixteenth, "16th"},
    {model::Dur::ThirtySecond, "32nd"},
    {model::Dur::SixtyFourth, "64th"},
}};

constexpr std::array<std::pair<model::TrackRole, std::string_view>, 5> roleNames{{
    {model::TrackRole::Melody, "melody"},
    {model::TrackRole::Chords, "chords"},
    {model::TrackRole::Bass, "bass"},
    {model::TrackRole::Solo, "solo"},
    {model::TrackRole::Other, "other"},
}};

constexpr int maxFret = 36;
constexpr int maxMidi = 127;
constexpr int maxCapo = 12;
constexpr int maxDots = 2;
constexpr int maxTupletPart = 64;
constexpr int maxKeyFifths = 7;
constexpr double maxTempoBpm = 1000.0; // no 40..220 clamp here (copy-spec.md section 3)
constexpr double maxSwingRatio = 0.75;
constexpr int maxTimeSigCount = 32;

// ---- small values ------------------------------------------------------------------------------------------

json timeSigToJson(const model::TimeSignature &timeSig)
{
    return {{"count", timeSig.count}, {"unit", timeSig.unit}};
}

model::TimeSignature timeSigFromJson(const json &node)
{
    const auto count = static_cast<int>(intAt(node, "count", 1, maxTimeSigCount));
    const auto unit = static_cast<int>(intAt(node, "unit", 1, 16));
    if (unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16) {
        throw Corrupt{"time_signature.unit is not 1, 2, 4, 8 or 16"};
    }
    return {count, unit};
}

json keySigToJson(const model::KeySignature &keySig)
{
    return {{"fifths", keySig.fifths}, {"minor", keySig.minor}};
}

model::KeySignature keySigFromJson(const json &node)
{
    return {static_cast<int>(intAt(node, "fifths", -maxKeyFifths, maxKeyFifths, 0)), boolAt(node, "minor", false)};
}

double tempoFromJson(const json &node, const char *name, std::optional<double> fallback)
{
    const double bpm = numberAt(node, name, 0.0, maxTempoBpm, fallback);
    if (bpm <= 0.0) {
        throw Corrupt{std::string(name) + " must be positive"};
    }
    return bpm;
}

// ---- tuning, beats -----------------------------------------------------------------------------------------

} // namespace

json tuningToJson(const theory::Tuning &tuning)
{
    return {{"strings", tuning.strings}, {"capo", tuning.capo}};
}

namespace
{

std::vector<theory::Midi> stringsFromJson(const json &node)
{
    const json &strings = arrayAt(node, "strings", static_cast<std::size_t>(theory::maxStrings));
    if (strings.size() < static_cast<std::size_t>(theory::minStrings)) {
        throw Corrupt{"tuning.strings has fewer than 4 strings"};
    }
    std::vector<theory::Midi> out;
    for (const json &midi : strings) {
        if (!midi.is_number_integer() || midi.get<int>() < 0 || midi.get<int>() > maxMidi) {
            throw Corrupt{"tuning.strings holds a value that is not a MIDI note"};
        }
        out.push_back(midi.get<int>());
    }
    return out;
}

} // namespace

Result<theory::Tuning> tuningFromJson(const json &node, const std::filesystem::path &path)
{
    try {
        theory::Tuning tuning;
        tuning.strings = stringsFromJson(node);
        tuning.capo = static_cast<int>(intAt(node, "capo", 0, maxCapo, 0));
        return tuning;
    } catch (const Corrupt &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    } catch (const json::exception &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    }
}

namespace
{

json noteToJson(const model::Note &note)
{
    return {{"id", note.id}, {"string", note.string}, {"fret", note.fret}, {"tie", note.tieToNext}};
}

model::Note noteFromJson(const json &node, int strings)
{
    model::Note note;
    note.id = idAt(node);
    note.string = static_cast<int>(intAt(node, "string", theory::firstString, strings));
    note.fret = static_cast<int>(intAt(node, "fret", theory::openFret, maxFret));
    note.tieToNext = boolAt(node, "tie", false);
    return note;
}

model::Beat beatFromJsonOrThrow(const json &node, std::size_t trackCount, int strings)
{
    model::Beat beat;
    beat.id = idAt(node);
    beat.track = static_cast<std::size_t>(intAt(node, "track", 0, static_cast<std::int64_t>(trackCount) - 1, 0));
    beat.voice = static_cast<int>(intAt(node, "voice", 1, theory::maxVoices, 1));
    beat.tick = intAt(node, "tick", 0, model::measureTicks({maxTimeSigCount, 1}), 0);
    beat.dur = nameAt(node, "dur", durNames, model::Dur::Quarter);
    beat.dots = static_cast<int>(intAt(node, "dots", 0, maxDots, 0));
    if (const auto tuplet = node.find("tuplet"); tuplet != node.end() && tuplet->is_object()) {
        beat.tuplet = model::Tuplet{static_cast<int>(intAt(*tuplet, "num", 1, maxTupletPart)),
                                    static_cast<int>(intAt(*tuplet, "numbase", 1, maxTupletPart))};
    }
    beat.rest = boolAt(node, "rest", false);
    for (const json &note : arrayAt(node, "notes", io::maxNotesPerBeat)) {
        beat.notes.push_back(noteFromJson(note, strings));
    }
    if (beat.rest != beat.notes.empty()) {
        throw Corrupt{"beat " + beat.id + ": rest and notes disagree"};
    }
    return beat;
}

} // namespace

json beatToJson(const model::Beat &beat)
{
    json notes = json::array();
    for (const model::Note &note : beat.notes) {
        notes.push_back(noteToJson(note));
    }
    json out{
        {"id", beat.id},
        {"track", beat.track},
        {"voice", beat.voice},
        {"tick", beat.tick},
        {"dur", jsonread::nameOf(durNames, beat.dur)},
        {"dots", beat.dots},
        {"rest", beat.rest},
        {"notes", std::move(notes)},
    };
    if (beat.tuplet) {
        out["tuplet"] = {{"num", beat.tuplet->num}, {"numbase", beat.tuplet->numbase}};
    }
    return out;
}

Result<model::Beat>
beatFromJson(const json &node, std::size_t trackCount, int strings, const std::filesystem::path &path)
{
    try {
        return beatFromJsonOrThrow(node, trackCount, strings);
    } catch (const Corrupt &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    } catch (const json::exception &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    }
}

// ---- tracks, sections, measures ----------------------------------------------------------------------------

namespace
{

json trackToJson(const model::Track &track)
{
    return {{"id", track.id},
            {"name", track.name},
            {"tuning", tuningToJson(track.tuning)},
            {"role", jsonread::nameOf(roleNames, track.role)}};
}

model::Track trackFromJson(const json &node, const std::filesystem::path &path)
{
    model::Track track;
    track.id = idAt(node);
    track.name = strAt(node, "name", io::maxNameBytes, "");
    auto tuning = tuningFromJson(node.value("tuning", json::object()), path);
    if (!tuning) {
        throw Corrupt{tuning.error().detail};
    }
    track.tuning = std::move(*tuning);
    track.role = nameAt(node, "role", roleNames, model::TrackRole::Other);
    return track;
}

json sectionToJson(const model::Section &section)
{
    return {{"id", section.id},
            {"start_bar", section.startMeasure},
            {"letter", section.letter},
            {"name", section.name}};
}

model::Section sectionFromJson(const json &node, std::size_t measureCount)
{
    model::Section section;
    section.id = idAt(node);
    section.startMeasure =
        static_cast<std::size_t>(intAt(node, "start_bar", 0, static_cast<std::int64_t>(measureCount) - 1));
    section.letter = strAt(node, "letter", io::maxNameBytes, "");
    section.name = strAt(node, "name", io::maxNameBytes, "");
    return section;
}

json chordToJson(const model::ChordSymbol &chord)
{
    return {{"id", chord.id}, {"tick", chord.tick}, {"text", chord.text}};
}

model::ChordSymbol chordFromJson(const json &node)
{
    model::ChordSymbol chord;
    chord.id = idAt(node);
    chord.tick = intAt(node, "tick", 0, model::measureTicks({maxTimeSigCount, 1}), 0);
    chord.text = strAt(node, "text", io::maxNameBytes);
    return chord;
}

json measureToJson(const model::Measure &measure)
{
    json out{{"id", measure.id}, {"chords", json::array()}, {"beats", json::array()}};
    if (measure.timeSig) {
        out["time_signature"] = timeSigToJson(*measure.timeSig);
    }
    if (measure.keySig) {
        out["key_signature"] = keySigToJson(*measure.keySig);
    }
    if (measure.tempoBpm) {
        out["tempo_bpm"] = *measure.tempoBpm;
    }
    if (measure.swingRatio) {
        out["swing_ratio"] = *measure.swingRatio;
    }
    for (const model::ChordSymbol &chord : measure.chords) {
        out["chords"].push_back(chordToJson(chord));
    }
    for (const model::Beat &beat : measure.beats) {
        out["beats"].push_back(beatToJson(beat));
    }
    return out;
}

void measureChangesFromJson(const json &node, model::Measure &measure)
{
    if (const auto it = node.find("time_signature"); it != node.end()) {
        measure.timeSig = timeSigFromJson(*it);
    }
    if (const auto it = node.find("key_signature"); it != node.end()) {
        measure.keySig = keySigFromJson(*it);
    }
    if (node.contains("tempo_bpm")) {
        measure.tempoBpm = tempoFromJson(node, "tempo_bpm", std::nullopt);
    }
    if (node.contains("swing_ratio")) {
        measure.swingRatio = numberAt(node, "swing_ratio", model::straightSwing, maxSwingRatio);
    }
}

model::Measure measureFromJson(const json &node, const model::Score &score)
{
    model::Measure measure;
    measure.id = idAt(node);
    measureChangesFromJson(node, measure);
    for (const json &chord : arrayAt(node, "chords", io::maxChordsPerMeasure)) {
        measure.chords.push_back(chordFromJson(chord));
    }
    const auto lastTrack = static_cast<std::int64_t>(score.tracks.size()) - 1;
    for (const json &beat : arrayAt(node, "beats", io::maxBeatsPerMeasure)) {
        const auto track = static_cast<std::size_t>(intAt(beat, "track", 0, lastTrack, 0));
        const int strings = score.tracks[track].tuning.stringCount();
        measure.beats.push_back(beatFromJsonOrThrow(beat, score.tracks.size(), strings));
    }
    return measure;
}

void sectionsFromJson(const json &node, model::Score &score)
{
    for (const json &section : arrayAt(node, "sections", io::maxListItems)) {
        score.sections.push_back(sectionFromJson(section, score.measures.size()));
        const std::size_t n = score.sections.size();
        if (n > 1 && score.sections[n - 1].startMeasure <= score.sections[n - 2].startMeasure) {
            throw Corrupt{"sections are not ascending by start_bar"};
        }
    }
}

model::Score scoreFromJsonOrThrow(const json &node, const std::filesystem::path &path)
{
    model::Score score;
    for (const json &track : arrayAt(node, "tracks", io::maxTracks)) {
        score.tracks.push_back(trackFromJson(track, path));
    }
    if (score.tracks.empty()) {
        throw Corrupt{"score.tracks is empty"};
    }
    score.initialTimeSig = timeSigFromJson(node.value("time_signature", json::object()));
    score.initialKeySig = keySigFromJson(node.value("key_signature", json::object()));
    score.initialTempoBpm = tempoFromJson(node, "tempo_bpm", score.initialTempoBpm);
    score.initialSwingRatio = numberAt(node, "swing_ratio", model::straightSwing, maxSwingRatio, model::straightSwing);
    for (const json &measure : arrayAt(node, "measures", io::maxMeasures)) {
        score.measures.push_back(measureFromJson(measure, score));
    }
    sectionsFromJson(node, score);
    return score;
}

} // namespace

json scoreToJson(const model::Score &score)
{
    json tracks = json::array();
    for (const model::Track &track : score.tracks) {
        tracks.push_back(trackToJson(track));
    }
    json sections = json::array();
    for (const model::Section &section : score.sections) {
        sections.push_back(sectionToJson(section));
    }
    json measures = json::array();
    for (const model::Measure &measure : score.measures) {
        measures.push_back(measureToJson(measure));
    }
    return {
        {"tracks", std::move(tracks)},
        {"sections", std::move(sections)},
        {"time_signature", timeSigToJson(score.initialTimeSig)},
        {"key_signature", keySigToJson(score.initialKeySig)},
        {"tempo_bpm", score.initialTempoBpm},
        {"swing_ratio", score.initialSwingRatio},
        {"measures", std::move(measures)},
    };
}

Result<model::Score> scoreFromJson(const json &node, const std::filesystem::path &path)
{
    try {
        return scoreFromJsonOrThrow(node, path);
    } catch (const Corrupt &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    } catch (const json::exception &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    }
}

} // namespace cc::project
