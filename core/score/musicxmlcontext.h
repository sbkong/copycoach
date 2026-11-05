// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/score/musicxmlimport.h"
#include "core/theory/tuning.h"

#include <pugixml.hpp>

#include <cstddef>
#include <optional>
#include <string>

// Shared state of one import run, split over musicxmlimport.cpp (document, parts, measures, directions),
// musicxmlnote.cpp (notes) and musicxmlharmony.cpp (chord symbols).
namespace cc::score::detail
{

constexpr int maxFret = 36;
constexpr int voicesPerStaff = 4; // Guitar Pro numbers staff 2's voices 5..8

/// What one part carries from measure to measure.
struct PartState {
    std::size_t track = 0;
    int divisions = 1; // per quarter note
    int tabStaff = 0; // staff number with the TAB clef, 0 when the part has none
    bool tuningKnown = false;
    int cursor = 0; // divisions from the measure start
    int lastStart = 0; // where the previous note began: a <chord/> member shares it
};

struct Importer {
    model::IdGenerator *ids = nullptr;
    std::string path;
    ScoreImport out;
    // values in force while walking part 1, for change detection
    model::TimeSignature time{4, 4};
    model::KeySignature key;
    double tempo = 0.0; // 0 = none seen yet
    double swing = model::straightSwing;
    bool voiceOverflowWarned = false;

    void warn(std::string text);
    [[nodiscard]] static model::Tick tickOf(const PartState &part, int divisionsFromStart);
    model::Measure &measureAt(std::size_t index);
};

/// Midi number of a <pitch> element; nullopt when the step is missing.
std::optional<theory::Midi> midiOf(const pugi::xml_node &pitch);

// musicxmlnote.cpp
void readNote(Importer &in, PartState &part, std::size_t measureIndex, const pugi::xml_node &note);
/// Beats ordered by (track, voice, tick) as the model asks (score.h), after a measure is complete.
void sortBeats(model::Measure &measure);

// musicxmlharmony.cpp
std::string harmonyText(const pugi::xml_node &harmony);

} // namespace cc::score::detail
