// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/score/musicxmlimport.h"

#include "core/io/limits.h"
#include "core/score/musicxmlcontext.h"
#include "core/theory/constants.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace cc::score
{

namespace detail
{

void Importer::warn(std::string text)
{
    out.warnings.push_back(std::move(text));
}

model::Tick Importer::tickOf(const PartState &part, int divisionsFromStart)
{
    const int divisions = part.divisions > 0 ? part.divisions : 1;
    const auto numerator = static_cast<model::Tick>(divisionsFromStart) * theory::ticksPerQuarter;
    return (numerator + (divisions / 2)) / divisions; // rounded: 5- and 7-tuplets do not divide 960 evenly (D-1)
}

model::Measure &Importer::measureAt(std::size_t index)
{
    auto &measures = out.score.measures;
    while (measures.size() <= index) {
        model::Measure measure;
        measure.id = ids->next();
        measures.push_back(std::move(measure));
    }
    return measures[index];
}

std::optional<theory::Midi> midiOf(const pugi::xml_node &pitch)
{
    constexpr std::array<std::pair<char, int>, 7> steps{
        {{'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}}};
    const std::string_view step = pitch.child("step").text().as_string();
    if (step.size() != 1) {
        return std::nullopt;
    }
    for (const auto &[letter, semitones] : steps) {
        if (letter == step[0]) {
            const int octave = pitch.child("octave").text().as_int(4);
            const int alter = pitch.child("alter").text().as_int(0);
            return ((octave + 1) * theory::semitonesPerOctave) + semitones + alter;
        }
    }
    return std::nullopt;
}

} // namespace detail

namespace
{

using detail::Importer;
using detail::PartState;

constexpr double defaultTempoBpm = 120.0; // what MusicXML assumes when no tempo is written

// --- attributes -------------------------------------------------------------------------------------------

/// <staff-details> for the TAB staff: tuning lines bottom up (line 1 = lowest string), capo.
void readTuning(Importer &in, PartState &part, const pugi::xml_node &details)
{
    theory::Tuning tuning;
    tuning.strings.clear();
    for (const pugi::xml_node &line : details.children("staff-tuning")) {
        pugi::xml_document scratch; // midiOf wants a <pitch>-shaped node; staff-tuning uses other names
        pugi::xml_node pitch = scratch.append_child("pitch");
        pitch.append_child("step").text().set(line.child("tuning-step").text().as_string());
        pitch.append_child("octave").text().set(line.child("tuning-octave").text().as_int(4));
        pitch.append_child("alter").text().set(line.child("tuning-alter").text().as_int(0));
        if (const auto midi = detail::midiOf(pitch)) {
            tuning.strings.insert(tuning.strings.begin(), *midi); // file lists low to high, strings[0] is string 1
        }
    }
    tuning.capo = details.child("capo").text().as_int(0);
    if (tuning.strings.size() < static_cast<std::size_t>(theory::minStrings)
        || tuning.strings.size() > static_cast<std::size_t>(theory::maxStrings)) {
        in.warn("track " + std::to_string(part.track + 1) + ": tuning with " + std::to_string(tuning.strings.size())
                + " strings ignored, standard tuning assumed");
        return;
    }
    in.out.score.tracks[part.track].tuning = std::move(tuning);
    part.tuningKnown = true;
}

void readMeasureChanges(Importer &in, std::size_t measureIndex, const pugi::xml_node &attributes)
{
    model::Measure &measure = in.measureAt(measureIndex);
    if (const pugi::xml_node time = attributes.child("time"); !time.empty()) {
        const model::TimeSignature sig{time.child("beats").text().as_int(4), time.child("beat-type").text().as_int(4)};
        if (measureIndex == 0) {
            in.out.score.initialTimeSig = sig;
        } else if (!(sig == in.time)) {
            measure.timeSig = sig;
        }
        in.time = sig;
    }
    if (const pugi::xml_node key = attributes.child("key"); !key.empty()) {
        const model::KeySignature sig{key.child("fifths").text().as_int(0),
                                      std::strcmp(key.child("mode").text().as_string(), "minor") == 0};
        if (measureIndex == 0) {
            in.out.score.initialKeySig = sig;
        } else if (!(sig == in.key)) {
            measure.keySig = sig;
        }
        in.key = sig;
    }
}

void readAttributes(Importer &in, PartState &part, std::size_t measureIndex, const pugi::xml_node &attributes)
{
    if (const int divisions = attributes.child("divisions").text().as_int(0); divisions > 0) {
        part.divisions = divisions;
    }
    for (const pugi::xml_node &clef : attributes.children("clef")) {
        if (std::strcmp(clef.child("sign").text().as_string(), "TAB") == 0) {
            part.tabStaff = clef.attribute("number").as_int(1);
        }
    }
    for (const pugi::xml_node &details : attributes.children("staff-details")) {
        const int number = details.attribute("number").as_int(1);
        if (!details.child("staff-tuning").empty() && (part.tabStaff == 0 || number == part.tabStaff)) {
            readTuning(in, part, details);
        }
    }
    if (part.track == 0) {
        readMeasureChanges(in, measureIndex, attributes);
    }
}

// --- directions ---------------------------------------------------------------------------------------------

void setTempo(Importer &in, std::size_t measureIndex, double bpm)
{
    if (bpm <= 0.0) {
        return;
    }
    if (in.tempo == 0.0 && measureIndex == 0) {
        in.out.score.initialTempoBpm = bpm;
    } else if (in.tempo == 0.0 || bpm != in.tempo) {
        in.measureAt(measureIndex).tempoBpm = bpm;
    }
    in.tempo = bpm;
}

void setSwing(Importer &in, std::size_t measureIndex, double ratio)
{
    if (measureIndex == 0) {
        in.out.score.initialSwingRatio = ratio;
    } else if (ratio != in.swing) {
        in.measureAt(measureIndex).swingRatio = ratio;
    }
    in.swing = ratio;
}

/// <swing> as a ratio (first of the pair over both), straight when it says so; nullopt when absent or odd.
std::optional<double> swingRatioOf(const pugi::xml_node &swing)
{
    if (swing.empty()) {
        return std::nullopt;
    }
    if (!swing.child("straight").empty()) {
        return model::straightSwing;
    }
    const double first = swing.child("first").text().as_double(1.0);
    const double second = swing.child("second").text().as_double(1.0);
    if (first <= 0.0 || second <= 0.0) {
        return std::nullopt;
    }
    return first / (first + second);
}

/// <sound tempo=".."> and <sound><swing>: tempo in quarter notes per minute.
void readSound(Importer &in, std::size_t measureIndex, const pugi::xml_node &sound)
{
    setTempo(in, measureIndex, sound.attribute("tempo").as_double(0.0));
    if (const auto ratio = swingRatioOf(sound.child("swing"))) {
        setSwing(in, measureIndex, *ratio);
    }
}

bool looksLikeLetter(const std::string &text)
{
    return !text.empty() && text.size() <= 2 && std::ranges::all_of(text, [](unsigned char c) {
        return std::isupper(c) != 0 || std::isdigit(c) != 0;
    });
}

void readRehearsal(Importer &in, std::size_t measureIndex, const std::string &text)
{
    auto &sections = in.out.score.sections;
    if (text.empty()) {
        return;
    }
    const auto existing = std::ranges::find_if(sections, [measureIndex](const model::Section &s) {
        return s.startMeasure == measureIndex;
    });
    if (existing != sections.end()) {
        if (existing->name.empty() && !looksLikeLetter(text)) {
            existing->name = text; // a letter and a name as two marks (our exporter writes them so)
        }
        return;
    }
    model::Section section{in.ids->next(), measureIndex, {}, {}};
    (looksLikeLetter(text) ? section.letter : section.name) = text;
    const auto it = std::ranges::find_if(sections, [measureIndex](const model::Section &s) {
        return s.startMeasure > measureIndex;
    });
    sections.insert(it, std::move(section));
}

void readWords(Importer &in, const ImportedText &where, const pugi::xml_node &type)
{
    for (const pugi::xml_node &words : type.children("words")) {
        const std::string text = words.text().as_string();
        if (!text.empty()) {
            in.out.texts.push_back({where.measure, where.tick, text});
        }
    }
}

void readDirection(Importer &in, const PartState &part, std::size_t measureIndex, const pugi::xml_node &direction)
{
    const model::Tick tick = Importer::tickOf(part, part.cursor + direction.child("offset").text().as_int(0));
    for (const pugi::xml_node &type : direction.children("direction-type")) {
        if (const pugi::xml_node metronome = type.child("metronome"); !metronome.empty()) {
            setTempo(in, measureIndex, metronome.child("per-minute").text().as_double(0.0));
        }
        if (part.track == 0) {
            readWords(in, ImportedText{measureIndex, tick, {}}, type);
        }
        if (const pugi::xml_node rehearsal = type.child("rehearsal"); !rehearsal.empty()) {
            readRehearsal(in, measureIndex, rehearsal.text().as_string());
        }
    }
    readSound(in, measureIndex, direction.child("sound"));
}

void readHarmony(Importer &in, const PartState &part, std::size_t measureIndex, const pugi::xml_node &harmony)
{
    const std::string text = detail::harmonyText(harmony);
    if (text.empty() || text.size() > io::maxNameBytes) {
        return;
    }
    const model::Tick tick = Importer::tickOf(part, part.cursor + harmony.child("offset").text().as_int(0));
    auto &chords = in.measureAt(measureIndex).chords;
    const auto it = std::ranges::find_if(chords, [tick](const model::ChordSymbol &chord) {
        return chord.tick >= tick;
    });
    if ((it != chords.end() && it->tick == tick) || chords.size() >= io::maxChordsPerMeasure) {
        return; // another part already wrote this beat's symbol
    }
    chords.insert(it, model::ChordSymbol{in.ids->next(), tick, text});
}

// --- measures and parts ---------------------------------------------------------------------------------------

void readMeasure(Importer &in, PartState &part, std::size_t measureIndex, const pugi::xml_node &measure)
{
    part.cursor = 0;
    for (const pugi::xml_node &child : measure.children()) {
        const std::string_view name = child.name();
        if (name == "attributes") {
            readAttributes(in, part, measureIndex, child);
        } else if (name == "note") {
            detail::readNote(in, part, measureIndex, child);
        } else if (name == "backup") {
            part.cursor = std::max(0, part.cursor - child.child("duration").text().as_int(0));
        } else if (name == "forward") {
            part.cursor += child.child("duration").text().as_int(0);
        } else if (name == "direction") {
            readDirection(in, part, measureIndex, child);
        } else if (name == "harmony") {
            readHarmony(in, part, measureIndex, child);
        } else if (name == "sound") {
            readSound(in, measureIndex, child);
        }
    }
    detail::sortBeats(in.measureAt(measureIndex));
}

Result<void> readPart(Importer &in, std::size_t track, const pugi::xml_node &part)
{
    PartState state;
    state.track = track;
    std::size_t index = 0;
    for (const pugi::xml_node &measure : part.children("measure")) {
        if (index >= io::maxMeasures) {
            return fail(ErrorCode::FileCorrupt, in.path, "more than " + std::to_string(io::maxMeasures) + " measures");
        }
        readMeasure(in, state, index, measure);
        ++index;
    }
    if (!state.tuningKnown) {
        in.warn("track " + std::to_string(track + 1) + ": no staff tuning in the file, standard tuning assumed");
    }
    return {};
}

void readCredit(Importer &in, const pugi::xml_node &credit)
{
    for (const pugi::xml_node &words : credit.children("credit-words")) {
        const std::string text = words.text().as_string();
        if (!text.empty()) {
            in.out.credits.push_back(text);
        }
    }
}

void readHeader(Importer &in, const pugi::xml_node &root)
{
    in.out.title = root.child("work").child("work-title").text().as_string();
    if (in.out.title.empty()) {
        in.out.title = root.child("movement-title").text().as_string();
    }
    for (const pugi::xml_node &credit : root.children("credit")) {
        readCredit(in, credit);
    }
}

Result<void> readParts(Importer &in, const pugi::xml_node &root)
{
    const pugi::xml_node partList = root.child("part-list");
    for (const pugi::xml_node &scorePart : partList.children("score-part")) {
        if (in.out.score.tracks.size() >= io::maxTracks) {
            return fail(ErrorCode::FileCorrupt, in.path, "more than " + std::to_string(io::maxTracks) + " parts");
        }
        in.out.score.tracks.push_back(model::Track{in.ids->next(),
                                                   scorePart.child("part-name").text().as_string(),
                                                   theory::Tuning::standardSix(),
                                                   model::TrackRole::Other});
    }
    if (in.out.score.tracks.empty()) {
        return fail(ErrorCode::FileCorrupt, in.path, "no parts");
    }
    std::size_t track = 0;
    for (const pugi::xml_node &part : root.children("part")) {
        if (track >= in.out.score.tracks.size()) {
            in.warn("a <part> without a <score-part> entry was skipped");
            break;
        }
        if (Result<void> r = readPart(in, track, part); !r) {
            return r;
        }
        ++track;
    }
    if (in.tempo == 0.0) {
        in.out.score.initialTempoBpm = defaultTempoBpm;
        in.warn("no tempo in the file, 120 bpm assumed");
    }
    return {};
}

} // namespace

Result<ScoreImport> importMusicXmlData(std::string_view xml, const std::filesystem::path &path, model::IdGenerator &ids)
{
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(xml.data(), xml.size());
    if (!parsed) {
        return fail(ErrorCode::FileCorrupt, path.string(), parsed.description());
    }
    const pugi::xml_node root = doc.child("score-partwise");
    if (root.empty()) {
        const bool timewise = !doc.child("score-timewise").empty();
        return fail(timewise ? ErrorCode::Unsupported : ErrorCode::FileCorrupt,
                    path.string(),
                    timewise ? "score-timewise is not supported" : "not a score-partwise document");
    }
    Importer in;
    in.ids = &ids;
    in.path = path.string();
    readHeader(in, root);
    if (Result<void> r = readParts(in, root); !r) {
        return std::unexpected(r.error());
    }
    return std::move(in.out);
}

Result<ScoreImport> importMusicXml(const std::filesystem::path &path, model::IdGenerator &ids)
{
    if (path.extension() == ".mxl") {
        return fail(ErrorCode::Unsupported, path.string(), "compressed MusicXML (.mxl) is not read yet");
    }
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::FileNotFound, path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return importMusicXmlData(buffer.str(), path, ids);
}

} // namespace cc::score
