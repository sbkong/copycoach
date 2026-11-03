// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/chordsymbol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace cc::theory
{

namespace
{

/// Walks the symbol left to right; every parse step consumes what it recognises and leaves the rest.
struct Cursor {
    std::string_view rest;

    bool take(std::string_view prefix)
    {
        if (!rest.starts_with(prefix)) {
            return false;
        }
        rest.remove_prefix(prefix.size());
        return true;
    }

    [[nodiscard]] bool atEnd() const
    {
        return rest.empty();
    }

    [[nodiscard]] bool peekDigit() const
    {
        return !rest.empty() && std::isdigit(static_cast<unsigned char>(rest.front())) != 0;
    }

    /// 0 when no number starts here.
    int takeNumber()
    {
        int value = 0;
        while (peekDigit()) {
            value = (value * 10) + (rest.front() - '0');
            rest.remove_prefix(1);
        }
        return value;
    }
};

constexpr std::array<int, 7> letterPitchClasses{9, 11, 0, 2, 4, 5, 7}; // A B C D E F G

std::optional<PitchClass> takeRoot(Cursor &cursor)
{
    if (cursor.rest.empty()) {
        return std::nullopt;
    }
    const char letter = cursor.rest.front();
    if (letter < 'A' || letter > 'G') {
        return std::nullopt;
    }
    cursor.rest.remove_prefix(1);
    PitchClass pitchClass = letterPitchClasses[static_cast<std::size_t>(letter - 'A')];
    if (cursor.take("#") || cursor.take("♯")) {
        pitchClass = transpose(pitchClass, 1);
    } else if (cursor.take("b") || cursor.take("♭")) {
        pitchClass = transpose(pitchClass, -1);
    }
    return pitchClass;
}

bool takeMajorSeventhMarker(Cursor &cursor)
{
    return cursor.take("maj") || cursor.take("Maj") || cursor.take("ma") || cursor.take("Δ") || cursor.take("M");
}

bool takeMinorMarker(Cursor &cursor)
{
    if (cursor.rest.starts_with("maj") || cursor.rest.starts_with("ma")) {
        return false; // "maj7" is not "m" + "aj7"
    }
    return cursor.take("min") || cursor.take("mi") || cursor.take("m") || cursor.take("-");
}

/// The quality words right after the root; the extension number follows.
void takeQuality(Cursor &cursor, ParsedChordSymbol &chord)
{
    if (takeMinorMarker(cursor)) {
        chord.kind = ChordKind::Minor;
        const bool parenthesised = cursor.take("(");
        if (takeMajorSeventhMarker(cursor)) {
            chord.majorSeventh = true;
        }
        if (parenthesised) {
            cursor.take(")");
        }
    } else if (cursor.take("dim") || cursor.take("°") || cursor.take("o")) {
        chord.kind = ChordKind::Diminished;
    } else if (cursor.take("ø")) {
        chord.kind = ChordKind::HalfDiminished;
    } else if (cursor.take("aug") || cursor.take("+")) {
        chord.kind = ChordKind::Augmented;
    } else if (takeMajorSeventhMarker(cursor)) {
        chord.majorSeventh = true;
    }
}

constexpr bool isExtension(int number)
{
    return number == 6 || number == 7 || number == 9 || number == 11 || number == 13 || number == 69;
}

/// "6", "7", "9", "11", "13", "69", "6/9"; a bare "7" on a major chord is a dominant seventh.
bool takeExtension(Cursor &cursor, ParsedChordSymbol &chord)
{
    if (!cursor.peekDigit()) {
        if (chord.majorSeventh || chord.kind == ChordKind::HalfDiminished) {
            chord.extension = 7; // "CΔ", "Cø" imply the seventh
        }
        return true;
    }
    const int number = cursor.takeNumber();
    if (!isExtension(number)) {
        return false;
    }
    if (number == 69 || (number == 6 && cursor.take("/9"))) {
        chord.extension = 6;
        chord.alterations.push_back({9, 0});
    } else {
        chord.extension = number;
    }
    if (chord.kind == ChordKind::Major && !chord.majorSeventh && chord.extension >= 7) {
        chord.kind = ChordKind::Dominant;
    }
    return true;
}

bool takeSuspension(Cursor &cursor, ParsedChordSymbol &chord)
{
    if (!cursor.take("sus")) {
        return false;
    }
    chord.kind = ChordKind::Suspended;
    const int degree = cursor.takeNumber();
    chord.susDegree = degree == 0 ? 4 : degree;
    return true;
}

bool takeOneAlteration(Cursor &cursor, ParsedChordSymbol &chord)
{
    if (cursor.take("alt")) {
        chord.altered = true;
        return true;
    }
    if (cursor.take("add")) {
        const int degree = cursor.takeNumber();
        chord.alterations.push_back({degree, 0});
        return degree != 0;
    }
    int semitones = 0;
    if (cursor.take("b") || cursor.take("♭") || cursor.take("-")) {
        semitones = -1;
    } else if (cursor.take("#") || cursor.take("♯") || cursor.take("+")) {
        semitones = 1;
    } else {
        return false;
    }
    const int degree = cursor.takeNumber();
    chord.alterations.push_back({degree, semitones});
    return degree == 5 || degree == 9 || degree == 11 || degree == 13;
}

/// "(b9)", "#11", "b13", "add9", "alt", "sus4" after the extension, in any order; parentheses and commas are noise.
bool takeAlterations(Cursor &cursor, ParsedChordSymbol &chord)
{
    while (!cursor.atEnd() && !cursor.rest.starts_with('/')) {
        if (cursor.take("(") || cursor.take(")") || cursor.take(",") || cursor.take(" ")) {
            continue;
        }
        if (takeSuspension(cursor, chord)) {
            continue;
        }
        if (!takeOneAlteration(cursor, chord)) {
            return false;
        }
    }
    return true;
}

bool takeBass(Cursor &cursor, ParsedChordSymbol &chord)
{
    if (!cursor.take("/")) {
        return cursor.atEnd();
    }
    chord.bass = takeRoot(cursor);
    return chord.bass.has_value() && cursor.atEnd();
}

/// Cm7b5 is a half-diminished chord; the b5 is its quality, not an alteration.
void foldHalfDiminished(ParsedChordSymbol &chord)
{
    if (chord.kind != ChordKind::Minor || chord.extension < 7 || chord.majorSeventh) {
        return;
    }
    const auto flatFive = std::ranges::find(chord.alterations, ChordAlteration{5, -1});
    if (flatFive == chord.alterations.end()) {
        return;
    }
    chord.alterations.erase(flatFive);
    chord.kind = ChordKind::HalfDiminished;
}

} // namespace

std::optional<ParsedChordSymbol> parseChordSymbol(std::string_view text)
{
    Cursor cursor{text};
    ParsedChordSymbol chord;
    const std::optional<PitchClass> root = takeRoot(cursor);
    if (!root) {
        return std::nullopt;
    }
    chord.root = *root;
    takeQuality(cursor, chord);
    takeSuspension(cursor, chord);
    if (!takeExtension(cursor, chord) || !takeAlterations(cursor, chord) || !takeBass(cursor, chord)) {
        return std::nullopt;
    }
    foldHalfDiminished(chord);
    return chord;
}

} // namespace cc::theory
