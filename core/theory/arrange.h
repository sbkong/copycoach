// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/fingering.h"
#include "core/theory/pitch.h"
#include "core/theory/tuning.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Fingering by dynamic programming (fingering.md section 5, copy-spec.md section 16.2 A1-6): every note's
// playable (string, fret) pairs are the states, the cheapest path through them is the fingering. The three
// patterns are three cost tables over the same engine: Vertical keeps one position (shifts cost most),
// Horizontal stays on a string (string changes cost most), Diagonal plays three or four notes per string and
// moves up the neck as the line rises (the routine's diagonal forms, routine-spec.md section 3). Pins fix a
// note's position; a note no string can sound gets no position and the chain restarts after it.
namespace cc::theory
{

struct ArrangeOptions {
    FingeringPattern pattern = FingeringPattern::Diagonal;
    int maxFret = 20;
    bool allowOpen = true; // open strings as candidates
};

/// One position per note (nullopt where the tuning cannot sound it). `pins` may be empty or one entry per
/// note; a pinned note uses that position when the tuning sounds the pitch there, otherwise it is arranged.
[[nodiscard]] std::vector<std::optional<StringFret>>
arrangeFingering(std::span<const Midi> notes,
                 const Tuning &tuning,
                 const ArrangeOptions &options,
                 std::span<const std::optional<StringFret>> pins = {});

} // namespace cc::theory
