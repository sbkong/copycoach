// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/score/meiwriter.h"

#include <pugixml.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Shared between meiwriter.cpp (document, scoreDef, sections, measures, control events) and meilayer.cpp
// (the layers: beams, tuplets, notes, tab groups).
namespace cc::score::detail
{

/// Letter, accidental and octave of a pitch as the key signature spells it.
struct Spelling {
    char pname = 'c';
    std::string accid; // "", "s", "f", "n"
    bool inKey = false; // the accidental is the key's own: write it as gestural, not printed
    int octave = 4;
};

Spelling spell(int midi, const model::KeySignature &key);

/// Measure walk state: what the layers need from the measure and what they hand back (ties).
struct MeasureContext {
    const model::Score *score = nullptr;
    const MeiOptions *options = nullptr;
    std::size_t measureIndex = 0;
    model::TimeSignature meter;
    model::KeySignature key;
    pugi::xml_node measureNode;
    std::vector<std::pair<std::string, std::string>> ties; // start id, end id, gathered by the staff layers
};

/// One staff of one track in one measure: writes <layer> children for each voice that has beats.
void writeStaffLayers(MeasureContext &ctx, std::size_t track, int staffNumber, bool tab);

std::string meiDur(model::Dur dur);

} // namespace cc::score::detail
