// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"

#include <cstddef>
#include <optional>

// Tick -> beat for the views (score-data-path.md section 5: "idsAt(absTick) is the model's answer"). The
// playhead needs the beat under a tick and where the next one starts, so it can slide between them.
namespace cc::model
{

struct LocatedBeat {
    std::size_t measure = 0;
    const Beat *beat = nullptr;
    Tick startTick = 0; // absolute
    Tick nextTick = 0; // absolute start of the next beat in this voice, or the end of the measure
};

/// The last beat of (track, voice) starting at or before `absTick`, searching back across measures; nullopt
/// before the first beat or outside the score.
std::optional<LocatedBeat> locateBeat(const Score &score, Tick absTick, std::size_t track, int voice = 1);

/// The measure containing an absolute tick, if any.
std::optional<std::size_t> measureAtTick(const Score &score, Tick absTick);

} // namespace cc::model
