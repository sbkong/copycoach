// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/edit/commands.h"
#include "core/model/score.h"

#include <string>
#include <vector>

// Edits on a copied measure, shared by the note commands and the prediction accept. Each returns whether the
// measure changed; the caller wraps the result in a MeasureEdit.
namespace cc::edit::ops
{

/// The beat at `at`, or where one would go to keep the (track, voice, tick) order.
std::vector<model::Beat>::iterator slotFor(std::vector<model::Beat> &beats, const BeatRef &at);
/// The beat exactly at `at`, or end().
std::vector<model::Beat>::iterator beatAt(std::vector<model::Beat> &beats, const BeatRef &at);

bool stringPlayable(const model::Score &score, std::size_t track, int string);

bool putNote(model::Measure &measure,
             const BeatRef &at,
             theory::StringFret note,
             model::Dur dur,
             model::IdGenerator &ids);
bool putChord(model::Measure &measure, model::Tick tick, const std::string &text, model::IdGenerator &ids);

/// The plain note value (no dots) closest to a tick length; a zero length counts as a quarter.
model::Dur nearestDur(model::Tick ticks);

} // namespace cc::edit::ops
