// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/score/musicxmlexport.h"

#include "core/model/duration.h"
#include "core/theory/constants.h"
#include "core/theory/pitch.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace cc::score
{

namespace
{

constexpr int divisions = static_cast<int>(theory::ticksPerQuarter); // durations are ticks
constexpr int tabStaff = 2;

constexpr std::array<const char *, 7> typeNames{"whole", "half", "quarter", "eighth", "16th", "32nd", "64th"};
constexpr std::array<std::pair<char, int>, 12> sharpSpelling{{{'C', 0},
                                                              {'C', 1},
                                                              {'D', 0},
                                                              {'D', 1},
                                                              {'E', 0},
                                                              {'F', 0},
                                                              {'F', 1},
                                                              {'G', 0},
                                                              {'G', 1},
                                                              {'A', 0},
                                                              {'A', 1},
                                                              {'B', 0}}};
constexpr std::array<std::pair<char, int>, 12> flatSpelling{{{'C', 0},
                                                             {'D', -1},
                                                             {'D', 0},
                                                             {'E', -1},
                                                             {'E', 0},
                                                             {'F', 0},
                                                             {'G', -1},
                                                             {'G', 0},
                                                             {'A', -1},
                                                             {'A', 0},
                                                             {'B', -1},
                                                             {'B', 0}}};

void setInt(pugi::xml_node node, const char *name, long long value)
{
    node.append_child(name).text().set(value);
}

void setText(pugi::xml_node node, const char *name, const std::string &value)
{
    node.append_child(name).text().set(value.c_str());
}

/// <step><alter><octave> of a midi note, flats in flat keys.
void writePitch(pugi::xml_node note, theory::Midi midi, bool flats)
{
    const auto &spelling = flats ? flatSpelling : sharpSpelling;
    const auto [step, alter] = spelling[static_cast<std::size_t>(theory::pitchClassOf(midi))];
    pugi::xml_node pitch = note.append_child("pitch");
    pitch.append_child("step").text().set(std::string(1, step).c_str());
    if (alter != 0) {
        setInt(pitch, "alter", alter);
    }
    setInt(pitch, "octave", (midi / theory::semitonesPerOctave) - 1);
}

/// "Bb" -> step B alter -1; returns the characters consumed.
std::size_t writeNoteName(pugi::xml_node parent, const char *stepName, const char *alterName, std::string_view text)
{
    if (text.empty() || text[0] < 'A' || text[0] > 'G') {
        return 0;
    }
    parent.append_child(stepName).text().set(std::string(1, text[0]).c_str());
    std::size_t used = 1;
    int alter = 0;
    while (used < text.size() && (text[used] == '#' || text[used] == 'b')) {
        alter += text[used] == '#' ? 1 : -1;
        ++used;
    }
    if (alter != 0) {
        setInt(parent, alterName, alter);
    }
    return used;
}

/// <harmony> that harmonyText (the importer) turns back into exactly this text: root, kind text, bass.
void writeHarmony(pugi::xml_node measure, const model::ChordSymbol &chord)
{
    pugi::xml_node harmony = measure.append_child("harmony");
    std::string_view text = chord.text;
    pugi::xml_node root = harmony.append_child("root");
    const std::size_t used = writeNoteName(root, "root-step", "root-alter", text);
    if (used == 0) { // not a chord symbol the spelling knows: keep the text whole on a C root
        root.append_child("root-step").text().set("C");
        harmony.append_child("kind").append_attribute("text").set_value(chord.text.c_str());
        harmony.child("kind").text().set("other");
        setInt(harmony, "offset", chord.tick);
        return;
    }
    text.remove_prefix(used);
    const std::size_t slash = text.find('/');
    const std::string kindText(text.substr(0, slash));
    pugi::xml_node kind = harmony.append_child("kind");
    kind.append_attribute("text").set_value(kindText.c_str());
    kind.text().set(kindText.empty() ? "major" : "other");
    if (slash != std::string_view::npos) {
        writeNoteName(harmony.append_child("bass"), "bass-step", "bass-alter", text.substr(slash + 1));
    }
    setInt(harmony, "offset", chord.tick);
}

void writeTempo(pugi::xml_node measure, double bpm)
{
    pugi::xml_node direction = measure.append_child("direction");
    direction.append_attribute("placement").set_value("above");
    pugi::xml_node metronome = direction.append_child("direction-type").append_child("metronome");
    metronome.append_child("beat-unit").text().set("quarter");
    setInt(metronome, "per-minute", std::llround(bpm));
    direction.append_child("sound").append_attribute("tempo").set_value(bpm);
}

void writeSwing(pugi::xml_node measure, double ratio)
{
    pugi::xml_node swing = measure.append_child("sound").append_child("swing");
    if (ratio == model::straightSwing) {
        swing.append_child("straight");
        return;
    }
    constexpr double parts = 1000.0; // first / (first + second) back within 0.0005 of the ratio
    const long long first = std::llround(ratio * parts);
    setInt(swing, "first", first);
    setInt(swing, "second", static_cast<long long>(parts) - first);
    swing.append_child("swing-type").text().set("eighth");
}

void writeRehearsal(pugi::xml_node measure, const std::string &text)
{
    pugi::xml_node direction = measure.append_child("direction");
    direction.append_attribute("placement").set_value("above");
    setText(direction.append_child("direction-type"), "rehearsal", text);
}

/// Clefs, staff count and the TAB staff's tuning; only the first measure of a part carries them.
void writeStaves(pugi::xml_node attributes, const theory::Tuning &tuning)
{
    setInt(attributes, "staves", tabStaff);
    pugi::xml_node clef = attributes.append_child("clef");
    clef.append_attribute("number").set_value(1);
    clef.append_child("sign").text().set("G");
    setInt(clef, "line", 2);
    setInt(clef, "clef-octave-change", -1);
    pugi::xml_node tab = attributes.append_child("clef");
    tab.append_attribute("number").set_value(tabStaff);
    tab.append_child("sign").text().set("TAB");
    setInt(tab, "line", 5);
    pugi::xml_node details = attributes.append_child("staff-details");
    details.append_attribute("number").set_value(tabStaff);
    setInt(details, "staff-lines", tuning.stringCount());
    for (int line = 1; line <= tuning.stringCount(); ++line) { // line 1 = the lowest string
        const theory::Midi midi = tuning.openPitch(tuning.stringCount() + 1 - line);
        pugi::xml_node node = details.append_child("staff-tuning"); // NOLINT(misc-const-correctness) appends
        node.append_attribute("line").set_value(line);
        const auto [step, alter] = sharpSpelling[static_cast<std::size_t>(theory::pitchClassOf(midi))];
        node.append_child("tuning-step").text().set(std::string(1, step).c_str());
        if (alter != 0) {
            setInt(node, "tuning-alter", alter);
        }
        setInt(node, "tuning-octave", (midi / theory::semitonesPerOctave) - 1);
    }
    if (tuning.capo != 0) {
        setInt(details, "capo", tuning.capo);
    }
}

void writeAttributes(pugi::xml_node measure, const model::Score &score, std::size_t index, const theory::Tuning &tuning)
{
    const model::Measure &m = score.measures[index];
    const bool first = index == 0;
    if (!first && !m.timeSig && !m.keySig) {
        return;
    }
    pugi::xml_node attributes = measure.append_child("attributes");
    if (first) {
        setInt(attributes, "divisions", divisions);
    }
    if (first || m.keySig) {
        const model::KeySignature key = score.keySigAt(index);
        pugi::xml_node node = attributes.append_child("key");
        setInt(node, "fifths", key.fifths);
        node.append_child("mode").text().set(key.minor ? "minor" : "major");
    }
    if (first || m.timeSig) {
        const model::TimeSignature time = score.timeSigAt(index);
        const pugi::xml_node node = attributes.append_child("time");
        setInt(node, "beats", time.count);
        setInt(node, "beat-type", time.unit);
    }
    if (first) {
        writeStaves(attributes, tuning);
    }
}

struct NoteWriter {
    pugi::xml_node measure;
    const theory::Tuning *tuning;
    bool flats;
    std::set<int> tiedStrings; // strings whose previous beat tied into this one

    void writeBeat(const model::Beat &beat)
    {
        if (beat.rest || beat.notes.empty()) {
            pugi::xml_node note = measure.append_child("note");
            note.append_child("rest");
            writeCommon(note, beat);
            tiedStrings.clear();
            return;
        }
        std::set<int> tiedNext;
        for (std::size_t i = 0; i < beat.notes.size(); ++i) {
            writeNote(beat, beat.notes[i], i > 0);
            if (beat.notes[i].tieToNext) {
                tiedNext.insert(beat.notes[i].string);
            }
        }
        tiedStrings = std::move(tiedNext);
    }

    static void writeCommon(pugi::xml_node note, const model::Beat &beat)
    {
        setInt(note, "duration", beat.ticks());
        setInt(note, "voice", beat.voice);
        note.append_child("type").text().set(typeNames[static_cast<std::size_t>(beat.dur)]);
        for (int d = 0; d < beat.dots; ++d) {
            note.append_child("dot");
        }
        if (beat.tuplet) {
            const pugi::xml_node mod = note.append_child("time-modification");
            setInt(mod, "actual-notes", beat.tuplet->num);
            setInt(mod, "normal-notes", beat.tuplet->numbase);
        }
        setInt(note, "staff", tabStaff); // the importer reads the TAB staff when there is one
    }

    void writeNote(const model::Beat &beat, const model::Note &n, bool chord)
    {
        pugi::xml_node note = measure.append_child("note");
        if (chord) {
            note.append_child("chord");
        }
        writePitch(note, tuning->pitchAt({n.string, n.fret}), flats);
        setInt(note, "duration", beat.ticks());
        const bool stop = tiedStrings.contains(n.string);
        if (stop) {
            note.append_child("tie").append_attribute("type").set_value("stop");
        }
        if (n.tieToNext) {
            note.append_child("tie").append_attribute("type").set_value("start");
        }
        setInt(note, "voice", beat.voice);
        note.append_child("type").text().set(typeNames[static_cast<std::size_t>(beat.dur)]);
        for (int d = 0; d < beat.dots; ++d) {
            note.append_child("dot");
        }
        if (beat.tuplet) {
            const pugi::xml_node mod = note.append_child("time-modification");
            setInt(mod, "actual-notes", beat.tuplet->num);
            setInt(mod, "normal-notes", beat.tuplet->numbase);
        }
        setInt(note, "staff", tabStaff); // the importer reads the TAB staff when there is one
        pugi::xml_node notations = note.append_child("notations");
        if (stop) {
            notations.append_child("tied").append_attribute("type").set_value("stop");
        }
        if (n.tieToNext) {
            notations.append_child("tied").append_attribute("type").set_value("start");
        }
        const pugi::xml_node technical = notations.append_child("technical");
        setInt(technical, "string", n.string);
        setInt(technical, "fret", n.fret);
    }
};

/// One voice of one track in the measure: forward over gaps, the beats in tick order.
model::Tick writeVoice(NoteWriter &writer, const model::Measure &m, std::size_t track, int voice)
{
    model::Tick cursor = 0;
    for (const model::Beat &beat : m.beats) {
        if (beat.track != track || beat.voice != voice) {
            continue;
        }
        if (beat.tick > cursor) {
            setInt(writer.measure.append_child("forward"), "duration", beat.tick - cursor);
        }
        writer.writeBeat(beat);
        cursor = beat.tick + beat.ticks();
    }
    return cursor;
}

void writeMeasureContent(NoteWriter &writer, const model::Score &score, std::size_t index, std::size_t track)
{
    const model::Measure &m = score.measures[index];
    for (const model::ChordSymbol &chord : m.chords) {
        if (track == 0) {
            writeHarmony(writer.measure, chord);
        }
    }
    std::set<int> voices;
    for (const model::Beat &beat : m.beats) {
        if (beat.track == track) {
            voices.insert(beat.voice);
        }
    }
    model::Tick written = 0;
    bool firstVoice = true;
    for (const int voice : voices) {
        if (!firstVoice && written > 0) {
            setInt(writer.measure.append_child("backup"), "duration", written);
        }
        writer.tiedStrings.clear();
        written = writeVoice(writer, m, track, voice);
        firstVoice = false;
    }
}

/// The letter, then the name as a second mark when both exist (the importer fills the name from it).
void writeSection(pugi::xml_node measure, const model::Section &section)
{
    writeRehearsal(measure, section.letter.empty() ? section.name : section.letter);
    if (!section.letter.empty() && !section.name.empty()) {
        writeRehearsal(measure, section.name);
    }
}

/// Tempo, swing and section marks: once, on the first part.
void writeMeasureMarks(pugi::xml_node measure, const model::Score &score, std::size_t index)
{
    const model::Measure &m = score.measures[index];
    if (index == 0 || m.tempoBpm) {
        writeTempo(measure, score.tempoAt(index));
    }
    if ((index == 0 && score.initialSwingRatio != model::straightSwing) || m.swingRatio) {
        writeSwing(measure, score.swingAt(index));
    }
    for (const model::Section &section : score.sections) {
        if (section.startMeasure == index) {
            writeSection(measure, section);
        }
    }
}

void writeMeasure(pugi::xml_node part, const model::Score &score, std::size_t index, std::size_t track)
{
    pugi::xml_node measure = part.append_child("measure");
    measure.append_attribute("number").set_value(static_cast<unsigned>(index + 1));
    const theory::Tuning &tuning = score.tracks[track].tuning;
    writeAttributes(measure, score, index, tuning);
    if (track == 0) {
        writeMeasureMarks(measure, score, index);
    }
    NoteWriter writer{measure, &tuning, score.keySigAt(index).fifths < 0, {}};
    writeMeasureContent(writer, score, index, track);
}

} // namespace

std::string exportMusicXml(const model::Score &score, const std::string &title)
{
    pugi::xml_document doc;
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version").set_value("1.0");
    decl.append_attribute("encoding").set_value("UTF-8");
    doc.append_child(pugi::node_doctype)
        .set_value(
            "score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
            "\"http://www.musicxml.org/dtds/partwise.dtd\"");
    pugi::xml_node root = doc.append_child("score-partwise");
    root.append_attribute("version").set_value("4.0");
    setText(root.append_child("work"), "work-title", title);
    pugi::xml_node list = root.append_child("part-list");
    for (std::size_t t = 0; t < score.tracks.size(); ++t) {
        const std::string id = "P" + std::to_string(t + 1);
        pugi::xml_node part = list.append_child("score-part");
        part.append_attribute("id").set_value(id.c_str());
        setText(part, "part-name", score.tracks[t].name.empty() ? "Guitar" : score.tracks[t].name);
    }
    for (std::size_t t = 0; t < score.tracks.size(); ++t) {
        pugi::xml_node part = root.append_child("part");
        part.append_attribute("id").set_value(("P" + std::to_string(t + 1)).c_str());
        for (std::size_t m = 0; m < score.measures.size(); ++m) {
            writeMeasure(part, score, m, t);
        }
    }
    std::ostringstream out;
    doc.save(out, "  ");
    return out.str();
}

Result<void> writeMusicXml(const std::filesystem::path &path, const model::Score &score, const std::string &title)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::WriteFailed, path.string());
    }
    file << exportMusicXml(score, title);
    return file.good() ? Result<void>{} : fail(ErrorCode::WriteFailed, path.string());
}

} // namespace cc::score
