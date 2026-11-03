// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/pitch.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Reads a chord symbol as jazz charts, Guitar Pro and MusicXML imports write it ("Dm7", "G7b9", "Bbmaj7#11",
// "C6/9", "F/A"). The model keeps the text (model::ChordSymbol); this parse feeds degree labels and transposition
// (copy-spec.md section 16.2 A1-2). A symbol the parser does not understand is still displayed as text.
namespace cc::theory
{

enum class ChordKind : std::uint8_t {
    Major, // C, C6, Cmaj7, Cmaj9 ...
    Minor, // Cm, Cm7, Cm6, CmMaj7
    Dominant, // C7, C9, C13, C7alt
    HalfDiminished, // Cm7b5, Cø
    Diminished, // Cdim, Cdim7, C°
    Augmented, // Caug, C+
    Suspended, // Csus4, Csus2, C7sus4
};

/// A raised or lowered (or added) degree: {9, -1} = b9, {11, +1} = #11, {9, 0} = add9.
struct ChordAlteration {
    int degree;
    int semitones;
    constexpr bool operator==(const ChordAlteration &) const = default;
};

struct ParsedChordSymbol {
    PitchClass root = 0;
    ChordKind kind = ChordKind::Major;
    int extension = 5; // 5 = triad, 6, 7, 9, 11, 13
    bool majorSeventh = false; // maj7 family (also minor-major: CmMaj7)
    int susDegree = 0; // 2 or 4 when kind == Suspended
    bool altered = false; // "alt"
    std::vector<ChordAlteration> alterations;
    std::optional<PitchClass> bass; // slash chord
};

/// nullopt when the text is not a chord symbol the parser knows (trailing junk counts as unknown).
std::optional<ParsedChordSymbol> parseChordSymbol(std::string_view text);

} // namespace cc::theory
