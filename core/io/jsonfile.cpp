// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/io/jsonfile.h"

#include <fstream>
#include <system_error>

namespace cc::io
{

Result<nlohmann::json> readJsonFile(const std::filesystem::path &path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return fail(ErrorCode::FileNotFound, path.string());
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > maxJsonFileBytes) {
        return fail(ErrorCode::FileCorrupt, path.string(), ec ? ec.message() : "file larger than the JSON limit");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::FileCorrupt, path.string(), "cannot open for reading");
    }
    nlohmann::json document = nlohmann::json::parse(in, nullptr, false);
    if (document.is_discarded()) {
        return fail(ErrorCode::FileCorrupt, path.string(), "JSON parse error");
    }
    return document;
}

Result<void> writeJsonFileAtomic(const std::filesystem::path &path, const nlohmann::json &document)
{
    const std::filesystem::path temp = path.string() + ".tmp";
    const std::filesystem::path backup = path.string() + ".bak";
    {
        std::ofstream out(temp, std::ios::binary); // ofstream truncates by default
        if (!out) {
            return fail(ErrorCode::WriteFailed, path.string(), "cannot open temp file");
        }
        out << document.dump(1) << '\n';
        if (!out) {
            return fail(ErrorCode::WriteFailed, path.string(), "write to temp file failed");
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(path, backup, ec);
        if (ec) {
            return fail(ErrorCode::WriteFailed, path.string(), "cannot keep backup: " + ec.message());
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        return fail(ErrorCode::WriteFailed, path.string(), "rename failed: " + ec.message());
    }
    return {};
}

Result<int> schemaVersionOf(const nlohmann::json &document, int maxSupported, const std::filesystem::path &path)
{
    const auto it = document.find("schema_version");
    if (it == document.end() || !it->is_number_integer()) {
        return fail(ErrorCode::FileCorrupt, path.string(), "schema_version missing or not an integer");
    }
    const int version = it->get<int>();
    if (version < 1) {
        return fail(ErrorCode::FileCorrupt, path.string(), "schema_version below 1");
    }
    if (version > maxSupported) {
        return fail(ErrorCode::SchemaTooNew, path.string(), "schema_version " + std::to_string(version));
    }
    return version;
}

} // namespace cc::io
