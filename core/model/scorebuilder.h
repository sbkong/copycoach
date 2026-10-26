// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "core/theory/tuning.h"

#include <vector>

// Builds a Score from a plain description: measures of beats, each beat a note value plus the positions it sounds.
// Used by the routine sequencer (generated scores) and by tests; editing commands come with the copy editor.
namespace cc::model
{

struct BeatSpec {
    Dur dur = Dur::Quarter;
    std::vector<theory::StringFret> notes; // empty = rest
    bool tieToNext = false; // every note of this beat ties into the same string's note on the next beat
};

struct MeasureSpec {
    std::vector<BeatSpec> beats; // played back to back from tick 0; must not exceed the measure
};

struct ScoreSpec {
    TimeSignature timeSig{4, 4};
    double tempoBpm = 60.0;
    theory::Tuning tuning = theory::Tuning::standardSix();
    std::vector<MeasureSpec> measures;
};

/// Ids come from `ids` so tests and golden files can be deterministic.
Score buildScore(const ScoreSpec &spec, IdGenerator &ids);

} // namespace cc::model
