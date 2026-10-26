// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstdint>
#include <expected>
#include <string>

// Failures are values (PLAN 4-2 principle 6, coding-standards.md section 6). core never builds user sentences:
// the UI translates `code` + `subject` (docs/ui/states.md section 4).
namespace cc
{

enum class ErrorCode : std::uint8_t {
    DeviceOpenFailed,
    DeviceBusy,
    FileNotFound,
    FileCorrupt,
    SchemaTooNew,
    ModelMissing,
    ModelCorrupt,
    Unsupported,
    DiskFull,
    WriteFailed,
};

struct Error {
    ErrorCode code;
    std::string subject; // identifier the user sentence names: file path, device name, field name (UTF-8)
    std::string detail; // log-only text (English, library messages); never shown
};

template<typename T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(ErrorCode code, std::string subject, std::string detail = {})
{
    return std::unexpected(Error{code, std::move(subject), std::move(detail)});
}

} // namespace cc
