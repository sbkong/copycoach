// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// How a line is laid on the neck (copy-spec.md section 16.2 A1-6, user decision 2026-11-03). The copy
// workspace's fingering prediction and the routine's scale forms share this vocabulary; the routine's
// definition file keeps its own mode id string "diagonal", which maps onto Diagonal here.
namespace cc::theory
{

enum class FingeringPattern : std::uint8_t {
    Vertical, // across the strings inside one position
    Horizontal, // along one string
    Diagonal, // shifting position as the line moves
};

/// "vertical" / "horizontal" / "diagonal": the setting value and the project file value.
std::string_view fingeringPatternName(FingeringPattern pattern);
std::optional<FingeringPattern> parseFingeringPattern(std::string_view name);

} // namespace cc::theory
