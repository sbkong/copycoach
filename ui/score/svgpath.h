// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QPainterPath>

#include <string_view>

namespace cc::ui
{

/// Parses an SVG path "d" string (M L H V C S Q T Z and their relative forms) into a QPainterPath. Arcs (A)
/// are drawn as straight lines and counted in `unsupportedArcs`: the SMuFL glyph fonts contain none, and the
/// test for that keeps it so.
QPainterPath parseSvgPath(std::string_view d, int &unsupportedArcs);

} // namespace cc::ui
