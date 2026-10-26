// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/scorebuilder.h"
#include "core/model/timemap.h"

#include <cstdint>
#include <vector>

// Turns routine content (slot voicings, scale forms) into a playable Score and tick-stamped note events
// (routine-spec.md section 1 "슬롯 길이 · Arp / Chord", audio-engine.md section 8).
namespace cc::routine
{

enum class PlaybackStyle : std::uint8_t {
    Chord, // whole voicing on beat 1, held for the measure
    Arp, // one note per beat from the lowest string, rests fill the measure; too many notes -> eighths
};

using Voicing = std::vector<theory::StringFret>; // lowest string first is the usual order; sorted here anyway

/// One measure per slot (8 for the loops). Measures are packed per `style` and the time signature.
std::vector<model::MeasureSpec>
slotMeasures(const std::vector<Voicing> &slots, const model::TimeSignature &timeSig, PlaybackStyle style);

/// Scale run: every note one beat, ascending then descending (the top note is not repeated), last measure padded.
std::vector<model::MeasureSpec> scaleRunMeasures(const Voicing &ascendingForm, const model::TimeSignature &timeSig);

struct NoteEvent {
    model::Tick onTick; // absolute
    model::Tick offTick; // absolute
    int string;
    int fret;
    theory::Midi pitch;
    std::size_t measureIndex;
};

/// All sounding notes of a score in tick order (rests and ties-to-next produce no separate events).
std::vector<NoteEvent> noteEvents(const model::Score &score);

} // namespace cc::routine
