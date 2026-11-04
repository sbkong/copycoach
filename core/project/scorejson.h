// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/model/score.h"
#include "core/theory/tuning.h"

#include <nlohmann/json.hpp>

#include <filesystem>

// The score part of project.json (copy-spec.md section 1 `score`): tracks, sections, initial values and
// measures with their changes, chords and beats. Reading validates every field against the model's ranges
// and core/io/limits.h; unknown fields are ignored (coding-standards.md section 19). `path` names the file
// in errors.
namespace cc::project
{

nlohmann::json scoreToJson(const model::Score &score);
Result<model::Score> scoreFromJson(const nlohmann::json &node, const std::filesystem::path &path);

nlohmann::json tuningToJson(const theory::Tuning &tuning);
Result<theory::Tuning> tuningFromJson(const nlohmann::json &node, const std::filesystem::path &path);

/// One beat with its notes (also used by phrases). `trackCount` bounds `track`, `strings` bounds `string`.
nlohmann::json beatToJson(const model::Beat &beat);
Result<model::Beat>
beatFromJson(const nlohmann::json &node, std::size_t trackCount, int strings, const std::filesystem::path &path);

} // namespace cc::project
