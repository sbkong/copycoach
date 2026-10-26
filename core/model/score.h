// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/duration.h"
#include "core/model/id.h"
#include "core/theory/tuning.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The event model (score-data-path.md section 2): one truth that the score, tab, fretboard and keyboard views
// project. Notes store (string, fret); pitch is derived through the track tuning (tuning.md section 9).
// v1 carries what routine playback needs; articulations and copy fields arrive with the copy redesign.
namespace cc::model
{

struct Note {
    Id id;
    int string; // 1 = highest
    int fret; // 0 = open, relative to the capo
    bool tieToNext = false;
};

struct Beat {
    Id id;
    int voice = 1;
    Tick tick = 0; // offset inside the measure
    Dur dur = Dur::Quarter;
    int dots = 0;
    std::optional<Tuplet> tuplet;
    bool rest = false;
    std::vector<Note> notes; // empty when rest

    [[nodiscard]] Tick ticks() const
    {
        return durationTicks(dur, dots, tuplet);
    }
};

struct Measure {
    Id id;
    std::optional<TimeSignature> timeSig; // set when it changes here
    std::optional<double> tempoBpm; // set when it changes here
    std::vector<Beat> beats; // voice 1 then voice 2, each ascending by tick
};

struct Track {
    Id id;
    std::string name;
    theory::Tuning tuning = theory::Tuning::standardSix();
};

struct Score {
    Track track; // v1: one track
    TimeSignature initialTimeSig{4, 4};
    double initialTempoBpm = 60.0;
    std::vector<Measure> measures;

    [[nodiscard]] TimeSignature timeSigAt(std::size_t measureIndex) const;
    [[nodiscard]] double tempoAt(std::size_t measureIndex) const;
    [[nodiscard]] Tick measureStartTick(std::size_t measureIndex) const; // absolute tick of the measure's first beat
    [[nodiscard]] Tick totalTicks() const;
};

} // namespace cc::model
