// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/duration.h"
#include "core/model/id.h"
#include "core/theory/tuning.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The event model (score-data-path.md section 2): one truth that the score, tab, fretboard and keyboard views
// project. Notes store (string, fret); pitch is derived through the track tuning (tuning.md section 9).
// Copy piece 15 (copy-spec.md section 16.2 A1) widened it: several tracks with a role, chord symbols and
// key / swing changes on the measure, named sections. Articulations and repeats arrive with the editor.
namespace cc::model
{

struct Note {
    Id id;
    int string = 1; // 1 = highest
    int fret = 0; // 0 = open, relative to the capo
    bool tieToNext = false;
};

struct Beat {
    Id id;
    std::size_t track = 0; // index into Score::tracks
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

/// Circle-of-fifths key signature: fifths > 0 sharps, < 0 flats (-7..7); `minor` only changes the spelling label.
struct KeySignature {
    int fifths = 0;
    bool minor = false;
    constexpr bool operator==(const KeySignature &) const = default;
};

/// A chord symbol as written ("Dm7", "G7b9", "C/E"); theory::parseChordSymbol reads it when a view needs degrees.
/// The text stays even when it does not parse, so nothing a user or an import wrote is lost.
struct ChordSymbol {
    Id id;
    Tick tick = 0; // offset inside the measure
    std::string text;
};

/// Straight eighths are 0.5: the first of a pair takes half the beat. Triplet swing is 2/3.
constexpr double straightSwing = 0.5;

struct Measure {
    Id id;
    std::optional<TimeSignature> timeSig; // set when it changes here
    std::optional<KeySignature> keySig; // set when it changes here
    std::optional<double> tempoBpm; // set when it changes here
    std::optional<double> swingRatio; // set when it changes here (straightSwing .. 0.75)
    std::vector<ChordSymbol> chords; // ascending by tick
    std::vector<Beat> beats; // by track, then voice 1 then voice 2, each ascending by tick
};

/// What a track holds in the copy workflow (copy-spec.md section 1). The routine's generated track is Other.
enum class TrackRole : std::uint8_t {
    Melody,
    Chords,
    Bass,
    Solo,
    Other,
};

struct Track {
    Id id;
    std::string name;
    theory::Tuning tuning = theory::Tuning::standardSix();
    TrackRole role = TrackRole::Other;
};

/// A named part of the form ("A", "Bridge"), anchored on one measure like a Guitar Pro section.
struct Section {
    Id id;
    std::size_t startMeasure = 0;
    std::string letter; // "A", "B" ... may be empty
    std::string name; // "Intro", "Solo" ... may be empty
};

struct Score {
    std::vector<Track> tracks; // at least one once built; beats refer to them by index
    std::vector<Section> sections; // ascending by startMeasure, at most one per measure
    TimeSignature initialTimeSig{4, 4};
    KeySignature initialKeySig{};
    double initialTempoBpm = 60.0;
    double initialSwingRatio = straightSwing;
    std::vector<Measure> measures;

    [[nodiscard]] const Track &primaryTrack() const; // tracks[0]; the routine's only track
    [[nodiscard]] Track &primaryTrack();
    [[nodiscard]] TimeSignature timeSigAt(std::size_t measureIndex) const;
    [[nodiscard]] KeySignature keySigAt(std::size_t measureIndex) const;
    [[nodiscard]] double tempoAt(std::size_t measureIndex) const;
    [[nodiscard]] double swingAt(std::size_t measureIndex) const;
    /// The section that contains the measure, if any section starts at or before it.
    [[nodiscard]] std::optional<std::size_t> sectionIndexAt(std::size_t measureIndex) const;
    [[nodiscard]] Tick measureStartTick(std::size_t measureIndex) const; // absolute tick of the measure's first beat
    [[nodiscard]] Tick totalTicks() const;
};

} // namespace cc::model
