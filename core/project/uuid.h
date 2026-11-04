// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <string>
#include <string_view>

// project_id (copy-spec.md section 16.2 B-3): a UUID v4 made here from std::random_device, no library.
namespace cc::project
{

/// Lowercase 8-4-4-4-12 form, version nibble 4, RFC 4122 variant.
std::string newUuidV4();

/// True for the 36-character lowercase or uppercase hex-and-hyphen shape (any version).
bool isUuid(std::string_view text);

} // namespace cc::project
