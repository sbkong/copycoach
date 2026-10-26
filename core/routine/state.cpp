// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/routine/state.h"

#include "core/io/jsonfile.h"
#include "core/theory/constants.h"

#include <nlohmann/json.hpp>

namespace cc::routine
{

namespace
{

using nlohmann::json;

Result<std::vector<theory::PitchClass>> parseKeys(const json &node, const std::filesystem::path &path)
{
    std::vector<theory::PitchClass> keys;
    if (!node.is_array()) {
        return fail(ErrorCode::FileCorrupt, path.string(), "keys is not an array");
    }
    for (const auto &k : node) {
        const auto pitch = k.is_string() ? theory::parsePitchClass(k.get<std::string>()) : std::nullopt;
        if (!pitch) {
            return fail(ErrorCode::FileCorrupt, path.string(), "key is not a pitch name");
        }
        keys.push_back(*pitch);
    }
    if (keys.empty()) {
        return fail(ErrorCode::FileCorrupt, path.string(), "keys is empty");
    }
    return keys;
}

Result<model::TimeSignature> parseTimeSig(const json &node, const std::filesystem::path &path)
{
    const int count = node.value("count", 4);
    const int unit = node.value("unit", 4);
    if (count < 1 || count > 32 || (unit != 1 && unit != 2 && unit != 4 && unit != 8 && unit != 16)) {
        return fail(ErrorCode::FileCorrupt, path.string(), "time_signature out of range");
    }
    return model::TimeSignature{count, unit};
}

} // namespace

Result<RoutineState> loadRoutineState(const std::filesystem::path &path)
{
    auto document = io::readJsonFile(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    const auto version = io::schemaVersionOf(*document, routineStateSchemaVersion, path);
    if (!version) {
        return std::unexpected(version.error());
    }
    RoutineState state;
    const json &doc = *document;
    state.schemaVersion = *version;
    state.moduleId = doc.value("module", state.moduleId);
    state.modeId = doc.value("mode", state.modeId);
    for (const auto &[name, value] : doc.value("options", json::object()).items()) {
        if (value.is_string()) {
            state.options[name] = value.get<std::string>();
        }
    }
    auto keys = parseKeys(doc.value("keys", json::array({"C"})), path);
    if (!keys) {
        return std::unexpected(keys.error());
    }
    state.keys = std::move(*keys);
    const auto current = theory::parsePitchClass(doc.value("current_key", "C"));
    state.currentKey = current.value_or(state.keys.front());
    state.tempoBpm = doc.value("tempo_bpm", state.tempoBpm);
    if (state.tempoBpm < theory::tempoMinBpm || state.tempoBpm > theory::tempoMaxBpm) {
        return fail(ErrorCode::FileCorrupt, path.string(), "tempo_bpm out of 40..220");
    }
    auto timeSig = parseTimeSig(doc.value("time_signature", json::object()), path);
    if (!timeSig) {
        return std::unexpected(timeSig.error());
    }
    state.timeSig = *timeSig;
    const json sounds = doc.value("sounds", json::object());
    state.metronomeSound = sounds.value("metronome", state.metronomeSound);
    state.guitarSound = sounds.value("guitar", state.guitarSound);
    state.pianoSound = sounds.value("piano", state.pianoSound);
    return state;
}

Result<void> saveRoutineState(const std::filesystem::path &path, const RoutineState &state)
{
    json keys = json::array();
    for (const theory::PitchClass key : state.keys) {
        keys.push_back(std::string(theory::flatName(key)));
    }
    const json document{
        {"schema_version", routineStateSchemaVersion},
        {"module", state.moduleId},
        {"mode", state.modeId},
        {"options", state.options},
        {"keys", keys},
        {"current_key", std::string(theory::flatName(state.currentKey))},
        {"tempo_bpm", state.tempoBpm},
        {"time_signature", {{"count", state.timeSig.count}, {"unit", state.timeSig.unit}}},
        {"sounds", {{"metronome", state.metronomeSound}, {"guitar", state.guitarSound}, {"piano", state.pianoSound}}},
    };
    return io::writeJsonFileAtomic(path, document);
}

} // namespace cc::routine
