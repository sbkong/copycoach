// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/project/project.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

// project.json on disk (copy-spec.md section 1, states.md section 5). Explicit save goes through
// io::writeJsonFileAtomic (one .bak) and removes the autosave; the autosave has its own writer that rotates
// nothing (copy-spec.md section 16.2 B-2). Reading rejects newer schemas, validates every known field and
// ignores unknown ones. Schema 1 is the first; migrations arrive with schema 2 (coding-standards.md section 20).
namespace cc::project
{

nlohmann::json projectToJson(const Project &project);
/// `path` names the file in errors; dangling optional references (phrase range, memo range, last session) are
/// dropped, a sync point naming an unknown source is an error.
Result<Project> projectFromJson(const nlohmann::json &document, const std::filesystem::path &path);

Result<Project> loadProject(const std::filesystem::path &folder);
Result<void> saveProject(const std::filesystem::path &folder, const Project &project);

Result<Project> loadAutosave(const std::filesystem::path &folder);
Result<void> saveAutosave(const std::filesystem::path &folder, const Project &project);
/// True when an autosave exists and is newer than project.json (or project.json is missing): offer recovery.
bool autosaveIsNewer(const std::filesystem::path &folder);

/// "2026-11-04T09:15:00Z", for last_session.saved_at.
std::string nowIso8601Utc();

} // namespace cc::project
