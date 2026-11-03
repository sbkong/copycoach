// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/fingering.h"

#include <array>
#include <cstddef>

namespace cc::theory
{

namespace
{

constexpr std::array<std::string_view, 3> patternNames{"vertical", "horizontal", "diagonal"};

} // namespace

std::string_view fingeringPatternName(FingeringPattern pattern)
{
    return patternNames[static_cast<std::size_t>(pattern)];
}

std::optional<FingeringPattern> parseFingeringPattern(std::string_view name)
{
    for (std::size_t i = 0; i < patternNames.size(); ++i) {
        if (patternNames[i] == name) {
            return static_cast<FingeringPattern>(i);
        }
    }
    return std::nullopt;
}

} // namespace cc::theory
