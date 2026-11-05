// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "core/model/timemap.h"

#include <cstddef>
#include <span>
#include <vector>

// Turning a beat prediction into the score's timing (copy-spec.md section 4 "박 예측"): predicted downbeat k
// becomes "bar k starts here", one sync point per bar, which pins the whole tick map to the recording.
namespace cc::ui
{

/// Bars the score lacks for the downbeats to land on; the accept command inserts them first.
[[nodiscard]] std::size_t measuresNeeded(const model::Score &score, std::span<const double> downbeats);
/// One sync point per downbeat that has a bar (downbeat k <-> the start tick of measure k), ascending.
[[nodiscard]] std::vector<model::SyncPoint> syncPointsFromDownbeats(const model::Score &score,
                                                                    std::span<const double> downbeats);

} // namespace cc::ui
