// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <string_view>

namespace cc
{

/// Application version as shown in the about dialog and written into saved files as app_version.
std::string_view appVersion();

} // namespace cc
