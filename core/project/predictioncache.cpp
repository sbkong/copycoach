// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/project/predictioncache.h"

#include "core/io/jsonfile.h"
#include "core/project/jsonread.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <system_error>

namespace cc::project
{

using nlohmann::json;
using namespace cc::project::jsonread;

namespace
{

constexpr std::size_t maxItems = 1000000; // a long recording's notes or frames, generously

json secondsArray(const std::vector<double> &values)
{
    json out = values; // an array of numbers (a braced return would nest it)
    return out;
}

std::vector<double> secondsAt(const json &node, const char *name)
{
    std::vector<double> out;
    for (const json &v : arrayAt(node, name, maxItems)) {
        if (!v.is_number()) {
            throw std::runtime_error(name);
        }
        out.push_back(v.get<double>());
    }
    return out;
}

json beatsToJson(const predict::BeatPrediction &beats)
{
    return {{"beats", secondsArray(beats.beats)},
            {"downbeats", secondsArray(beats.downbeats)},
            {"seconds", beats.seconds}};
}

predict::BeatPrediction beatsFromJson(const json &node)
{
    predict::BeatPrediction beats;
    beats.beats = secondsAt(node, "beats");
    beats.downbeats = secondsAt(node, "downbeats");
    beats.seconds = numberAt(node, "seconds", 0.0, 1e7);
    return beats;
}

json notesToJson(const std::vector<predict::NoteEvent> &notes)
{
    json out = json::array();
    for (const predict::NoteEvent &note : notes) {
        out.push_back({{"start", note.startSeconds},
                       {"end", note.endSeconds},
                       {"midi", note.midi},
                       {"amplitude", note.amplitude}});
    }
    return out;
}

std::vector<predict::NoteEvent> notesFromJson(const json &node)
{
    std::vector<predict::NoteEvent> notes;
    for (const json &item : arrayAt(node, "notes", maxItems)) {
        predict::NoteEvent note;
        note.startSeconds = numberAt(item, "start", 0.0, 1e7);
        note.endSeconds = numberAt(item, "end", 0.0, 1e7);
        note.midi = static_cast<int>(intAt(item, "midi", 0, 127));
        note.amplitude = numberAt(item, "amplitude", 0.0, 1.0);
        notes.push_back(note);
    }
    return notes;
}

json chordsToJson(const std::vector<predict::ChordSegment> &chords)
{
    json out = json::array();
    for (const predict::ChordSegment &segment : chords) {
        out.push_back({{"start", segment.startSeconds}, {"end", segment.endSeconds}, {"label", segment.label}});
    }
    return out;
}

std::vector<predict::ChordSegment> chordsFromJson(const json &node)
{
    std::vector<predict::ChordSegment> chords;
    for (const json &item : arrayAt(node, "chords", maxItems)) {
        predict::ChordSegment segment;
        segment.startSeconds = numberAt(item, "start", 0.0, 1e7);
        segment.endSeconds = numberAt(item, "end", 0.0, 1e7);
        segment.label =
            static_cast<int>(intAt(item, "label", 0, static_cast<std::int64_t>(predict::chordMiniClasses) - 1));
        chords.push_back(segment);
    }
    return chords;
}

PredictionCache fromJsonOrThrow(const json &doc)
{
    if (intAt(doc, "schema_version", 1, predictionCacheVersion) != predictionCacheVersion) {
        throw std::runtime_error("schema_version");
    }
    PredictionCache cache;
    if (doc.contains("beats")) {
        cache.beats = beatsFromJson(doc.at("beats"));
    }
    if (doc.contains("notes")) {
        cache.notes = notesFromJson(doc);
    }
    if (doc.contains("chords")) {
        cache.chords = chordsFromJson(doc);
    }
    return cache;
}

} // namespace

Result<PredictionCache> loadPredictionCache(const std::filesystem::path &path)
{
    Result<json> doc = io::readJsonFile(path);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    try {
        return fromJsonOrThrow(*doc);
    } catch (const std::exception &e) {
        return fail(ErrorCode::FileCorrupt, e.what(), path.string());
    }
}

Result<void> savePredictionCache(const std::filesystem::path &path, const PredictionCache &cache)
{
    if (cache.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return {};
    }
    json doc{{"schema_version", predictionCacheVersion}};
    if (cache.beats) {
        doc["beats"] = beatsToJson(*cache.beats);
    }
    if (cache.notes) {
        doc["notes"] = notesToJson(*cache.notes);
    }
    if (cache.chords) {
        doc["chords"] = chordsToJson(*cache.chords);
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return io::writeJsonFileAtomic(path, doc);
}

} // namespace cc::project
