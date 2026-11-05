// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"

#include <string>

// The event model -> MEI for Verovio (score-data-path.md section 4): deterministic, whole document each time.
// Every element that a view may need to find carries an xml:id derived from the model id, so geometry comes
// back without a lookup table: m-<measure> · b-<beat> (tabGrp) · bs-<beat> (staff-side chord or rest) ·
// n-<note> (tab) · ns-<note> (staff) · s-<section>. Each track is a staff pair (notation above its TAB) or the
// TAB alone. Swing, articulations and repeats have no MEI yet (playback and editor pieces).
namespace cc::score
{

struct MeiOptions {
    bool tabOnly = false; // one TAB staff per track, no notation staff
};

std::string writeMei(const model::Score &score, const MeiOptions &options = {});

/// Staff number (1-based, as MEI counts them) of a track's notation staff and TAB staff under these options.
int notationStaffOf(std::size_t track, const MeiOptions &options);
int tabStaffOf(std::size_t track, const MeiOptions &options);

} // namespace cc::score
