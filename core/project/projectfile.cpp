// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/project/projectfile.h"

#include "core/io/jsonfile.h"
#include "core/io/limits.h"
#include "core/project/jsonread.h"
#include "core/project/scorejson.h"
#include "core/project/uuid.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace cc::project
{

namespace
{

using jsonread::arrayAt;
using jsonread::Corrupt;
using jsonread::idAt;
using jsonread::intAt;
using jsonread::nameAt;
using jsonread::numberAt;
using jsonread::optionalIdAt;
using jsonread::strAt;
using nlohmann::json;

constexpr std::array<std::pair<SourceKind, std::string_view>, 2> kindNames{{
    {SourceKind::Audio, "audio"},
    {SourceKind::Video, "video"},
}};

constexpr double maxPitchCents = 100.0;
constexpr int maxAudioTrack = 64;
constexpr double maxSeconds = 24.0 * 3600.0;
constexpr int maxPriority = 1000;
constexpr std::size_t maxSavedAtBytes = 64;

// ---- to JSON -----------------------------------------------------------------------------------------------

json sourcesToJson(const Project &project)
{
    json out = json::array();
    for (const Source &source : project.sources) {
        out.push_back({{"id", source.id},
                       {"kind", jsonread::nameOf(kindNames, source.kind)},
                       {"path", source.path},
                       {"hash", source.hash},
                       {"audio_track", source.audioTrack},
                       {"label", source.label}});
    }
    return out;
}

json syncPointsToJson(const Project &project)
{
    json out = json::array();
    for (const SourceSyncPoint &sync : project.syncPoints) {
        out.push_back({{"source_id", sync.sourceId}, {"tick", sync.point.tick}, {"time_s", sync.point.seconds}});
    }
    return out;
}

json rangesToJson(const Project &project)
{
    json out = json::array();
    for (const Range &range : project.ranges) {
        out.push_back({{"id", range.id},
                       {"name", range.name},
                       {"bar_from", range.barFrom},
                       {"bar_to", range.barTo},
                       {"priority", range.priority},
                       {"speed_pct", range.speedPct},
                       {"pitch_semitones", range.pitchSemitones}});
    }
    return out;
}

json keysToJson(const std::vector<theory::PitchClass> &keys)
{
    json out = json::array();
    for (const theory::PitchClass key : keys) {
        out.push_back(std::string(theory::flatName(key)));
    }
    return out;
}

json phrasesToJson(const Project &project)
{
    json out = json::array();
    for (const Phrase &phrase : project.phrases) {
        json beats = json::array();
        for (const model::Beat &beat : phrase.beats) {
            beats.push_back(beatToJson(beat));
        }
        out.push_back({{"id", phrase.id},
                       {"range_id", phrase.rangeId.value_or("")},
                       {"tuning", tuningToJson(phrase.tuning)},
                       {"beats", std::move(beats)},
                       {"chord_context", phrase.chordContext},
                       {"fingering_pattern", theory::fingeringPatternName(phrase.fingeringPattern)},
                       {"keys_done", keysToJson(phrase.keysDone)}});
    }
    return out;
}

json lastSessionToJson(const LastSession &session)
{
    return {{"range_id", session.rangeId.value_or("")},
            {"playhead_tick", session.playheadTick},
            {"speed_pct", session.speedPct},
            {"source_id", session.sourceId.value_or("")},
            {"saved_at", session.savedAt}};
}

// ---- from JSON ---------------------------------------------------------------------------------------------

Source sourceFromJson(const json &node)
{
    Source source;
    source.id = idAt(node);
    source.kind = nameAt(node, "kind", kindNames, SourceKind::Audio);
    source.path = strAt(node, "path", io::maxNameBytes);
    source.hash = strAt(node, "hash", io::maxNameBytes, "");
    source.audioTrack = static_cast<int>(intAt(node, "audio_track", 0, maxAudioTrack, 0));
    source.label = strAt(node, "label", io::maxNameBytes, "");
    return source;
}

Reference referenceFromJson(const json &node)
{
    Reference reference;
    reference.pitchCents = numberAt(node, "pitch_cents", -maxPitchCents, maxPitchCents, 0.0);
    const auto note = theory::parsePitchClass(strAt(node, "tuning_note", io::maxNameBytes, "E"));
    if (!note) {
        throw Corrupt{"reference.tuning_note is not a pitch name"};
    }
    reference.tuningNote = *note;
    return reference;
}

bool hasSource(const Project &project, const model::Id &id)
{
    return std::ranges::any_of(project.sources, [&id](const Source &s) {
        return s.id == id;
    });
}

bool hasRange(const Project &project, const model::Id &id)
{
    return std::ranges::any_of(project.ranges, [&id](const Range &r) {
        return r.id == id;
    });
}

void syncPointsFromJson(const json &node, Project &project)
{
    for (const json &sync : arrayAt(node, "sync_points", io::maxListItems)) {
        SourceSyncPoint point;
        point.sourceId = strAt(sync, "source_id", io::maxNameBytes);
        if (!hasSource(project, point.sourceId)) {
            throw Corrupt{"sync point names an unknown source: " + point.sourceId};
        }
        point.point.tick =
            intAt(sync, "tick", 0, model::measureTicks({32, 1}) * static_cast<model::Tick>(io::maxMeasures));
        point.point.seconds = numberAt(sync, "time_s", 0.0, maxSeconds);
        project.syncPoints.push_back(point);
    }
}

void rangesFromJson(const json &node, Project &project)
{
    const auto lastBar = static_cast<std::int64_t>(project.score.measures.size()) - 1;
    for (const json &item : arrayAt(node, "ranges", io::maxListItems)) {
        Range range;
        range.id = idAt(item);
        range.name = strAt(item, "name", io::maxNameBytes, "");
        range.barFrom = static_cast<std::size_t>(intAt(item, "bar_from", 0, lastBar));
        range.barTo =
            static_cast<std::size_t>(intAt(item, "bar_to", static_cast<std::int64_t>(range.barFrom), lastBar));
        range.priority = static_cast<int>(intAt(item, "priority", 0, maxPriority, 0));
        range.speedPct = static_cast<int>(intAt(item, "speed_pct", speedPctMin, speedPctMax, 100));
        range.pitchSemitones =
            static_cast<int>(intAt(item, "pitch_semitones", -pitchSemitonesMax, pitchSemitonesMax, 0));
        project.ranges.push_back(std::move(range));
    }
}

std::vector<theory::PitchClass> keysFromJson(const json &node)
{
    std::vector<theory::PitchClass> keys;
    for (const json &key : arrayAt(node, "keys_done", static_cast<std::size_t>(theory::pitchClassCount))) {
        const auto pitch = key.is_string() ? theory::parsePitchClass(key.get<std::string>()) : std::nullopt;
        if (!pitch) {
            throw Corrupt{"keys_done holds a value that is not a pitch name"};
        }
        keys.push_back(*pitch);
    }
    return keys;
}

Phrase phraseFromJson(const json &node, const Project &project, const std::filesystem::path &path)
{
    Phrase phrase;
    phrase.id = idAt(node);
    phrase.rangeId = optionalIdAt(node, "range_id");
    if (phrase.rangeId && !hasRange(project, *phrase.rangeId)) {
        phrase.rangeId.reset(); // the range was deleted: the phrase stays
    }
    auto tuning = tuningFromJson(node.value("tuning", json::object()), path);
    if (!tuning) {
        throw Corrupt{tuning.error().detail};
    }
    phrase.tuning = std::move(*tuning);
    for (const json &beat : arrayAt(node, "beats", io::maxBeatsPerMeasure)) {
        auto parsed = beatFromJson(beat, 1, phrase.tuning.stringCount(), path);
        if (!parsed) {
            throw Corrupt{parsed.error().detail};
        }
        phrase.beats.push_back(std::move(*parsed));
    }
    phrase.chordContext = strAt(node, "chord_context", io::maxNameBytes, "");
    const auto pattern = theory::parseFingeringPattern(strAt(node, "fingering_pattern", io::maxNameBytes, "vertical"));
    if (!pattern) {
        throw Corrupt{"fingering_pattern has an unknown value"};
    }
    phrase.fingeringPattern = *pattern;
    phrase.keysDone = keysFromJson(node);
    return phrase;
}

Memos memosFromJson(const json &node, const Project &project)
{
    Memos memos;
    memos.project = strAt(node, "project", io::maxTextBytes, "");
    const json ranges = node.value("ranges", json::object());
    if (!ranges.is_object()) {
        throw Corrupt{"memos.ranges is not an object"};
    }
    for (const auto &[id, text] : ranges.items()) {
        if (!text.is_string() || text.get_ref<const std::string &>().size() > io::maxTextBytes) {
            throw Corrupt{"memos.ranges holds a value that is not a text"};
        }
        if (hasRange(project, id)) { // a memo of a deleted range goes with it
            memos.ranges[id] = text.get<std::string>();
        }
    }
    return memos;
}

LastSession lastSessionFromJson(const json &node, const Project &project)
{
    LastSession session;
    session.rangeId = optionalIdAt(node, "range_id");
    if (session.rangeId && !hasRange(project, *session.rangeId)) {
        session.rangeId.reset();
    }
    session.playheadTick =
        intAt(node, "playhead_tick", 0, model::measureTicks({32, 1}) * static_cast<model::Tick>(io::maxMeasures), 0);
    session.speedPct = static_cast<int>(intAt(node, "speed_pct", speedPctMin, speedPctMax, 100));
    session.sourceId = optionalIdAt(node, "source_id");
    if (session.sourceId && !hasSource(project, *session.sourceId)) {
        session.sourceId.reset();
    }
    session.savedAt = strAt(node, "saved_at", maxSavedAtBytes, "");
    return session;
}

Project projectFromJsonOrThrow(const json &doc, int version, const std::filesystem::path &path)
{
    Project project;
    project.schemaVersion = version;
    project.projectId = strAt(doc, "project_id", maxSavedAtBytes);
    if (!isUuid(project.projectId)) {
        throw Corrupt{"project_id is not a UUID"};
    }
    for (const json &source : arrayAt(doc, "sources", io::maxListItems)) {
        project.sources.push_back(sourceFromJson(source));
    }
    project.reference = referenceFromJson(doc.value("reference", json::object()));
    auto score = scoreFromJson(doc.value("score", json::object()), path);
    if (!score) {
        throw Corrupt{score.error().detail};
    }
    project.score = std::move(*score);
    syncPointsFromJson(doc, project);
    rangesFromJson(doc, project);
    for (const json &phrase : arrayAt(doc, "phrases", io::maxListItems)) {
        project.phrases.push_back(phraseFromJson(phrase, project, path));
    }
    project.memos = memosFromJson(doc.value("memos", json::object()), project);
    project.lyrics = strAt(doc, "lyrics", io::maxTextBytes, "");
    project.stage = static_cast<int>(intAt(doc, "stage", stageMin, stageMax, stageMin));
    project.lastSession = lastSessionFromJson(doc.value("last_session", json::object()), project);
    return project;
}

Result<void> writeWithoutBackup(const std::filesystem::path &path, const json &document)
{
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        if (!out) {
            return fail(ErrorCode::WriteFailed, path.string(), "cannot open temp file");
        }
        out << document.dump(1) << '\n';
        if (!out) {
            return fail(ErrorCode::WriteFailed, path.string(), "write to temp file failed");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec); // replaces an existing file on every platform we build for
    if (ec) {
        return fail(ErrorCode::WriteFailed, path.string(), "rename failed: " + ec.message());
    }
    return {};
}

} // namespace

// ---- public ------------------------------------------------------------------------------------------------

json projectToJson(const Project &project)
{
    json memoRanges = json::object();
    for (const auto &[id, text] : project.memos.ranges) {
        memoRanges[id] = text;
    }
    return {
        {"schema_version", projectSchemaVersion},
        {"project_id", project.projectId},
        {"sources", sourcesToJson(project)},
        {"reference",
         {{"pitch_cents", project.reference.pitchCents},
          {"tuning_note", std::string(theory::flatName(project.reference.tuningNote))}}},
        {"score", scoreToJson(project.score)},
        {"sync_points", syncPointsToJson(project)},
        {"ranges", rangesToJson(project)},
        {"phrases", phrasesToJson(project)},
        {"memos", {{"project", project.memos.project}, {"ranges", std::move(memoRanges)}}},
        {"lyrics", project.lyrics},
        {"stage", project.stage},
        {"last_session", lastSessionToJson(project.lastSession)},
    };
}

Result<Project> projectFromJson(const json &document, const std::filesystem::path &path)
{
    const auto version = io::schemaVersionOf(document, projectSchemaVersion, path);
    if (!version) {
        return std::unexpected(version.error());
    }
    try {
        return projectFromJsonOrThrow(document, *version, path);
    } catch (const Corrupt &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    } catch (const json::exception &e) {
        return fail(ErrorCode::FileCorrupt, path.string(), e.what());
    }
}

Result<Project> loadProject(const std::filesystem::path &folder)
{
    const auto path = folder / projectFileName;
    auto document = io::readJsonFile(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    return projectFromJson(*document, path);
}

Result<void> saveProject(const std::filesystem::path &folder, const Project &project)
{
    const auto written = io::writeJsonFileAtomic(folder / projectFileName, projectToJson(project));
    if (!written) {
        return written;
    }
    std::error_code ec;
    std::filesystem::remove(folder / autosaveFileName, ec); // states.md section 5: explicit save clears it
    return {};
}

Result<Project> loadAutosave(const std::filesystem::path &folder)
{
    const auto path = folder / autosaveFileName;
    auto document = io::readJsonFile(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    return projectFromJson(*document, path);
}

Result<void> saveAutosave(const std::filesystem::path &folder, const Project &project)
{
    return writeWithoutBackup(folder / autosaveFileName, projectToJson(project));
}

bool autosaveIsNewer(const std::filesystem::path &folder)
{
    std::error_code ec;
    const auto autosave = folder / autosaveFileName;
    if (!std::filesystem::exists(autosave, ec)) {
        return false;
    }
    const auto saved = folder / projectFileName;
    if (!std::filesystem::exists(saved, ec)) {
        return true;
    }
    // Equal times count as newer: an autosave that survived a save is worth offering, the user decides.
    return std::filesystem::last_write_time(autosave, ec) >= std::filesystem::last_write_time(saved, ec);
}

std::string nowIso8601Utc()
{
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%FT%T}Z", now);
}

} // namespace cc::project
