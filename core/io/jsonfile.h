// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>

// JSON files with schema_version: read with limits (coding-standards.md section 19), write atomically with one
// backup (naming.md section 5, states.md section 5).
namespace cc::io
{

/// Largest file this loader accepts (16 MiB); bigger files are reported as corrupt rather than parsed.
constexpr std::uintmax_t maxJsonFileBytes = std::uintmax_t{16} * 1024 * 1024;

/// Parses the file. FileNotFound when absent, FileCorrupt (subject = path, detail = parser message) otherwise.
Result<nlohmann::json> readJsonFile(const std::filesystem::path &path);

/// Writes to `<path>.tmp`, keeps the previous file as `<path>.bak`, then renames. WriteFailed on any step.
Result<void> writeJsonFileAtomic(const std::filesystem::path &path, const nlohmann::json &document);

/// Reads `schema_version` (must be an integer 1..maxSupported). SchemaTooNew when the file is from a newer app.
Result<int> schemaVersionOf(const nlohmann::json &document, int maxSupported, const std::filesystem::path &path);

} // namespace cc::io
