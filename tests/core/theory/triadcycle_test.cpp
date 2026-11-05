// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 4.6 completion criteria for module 03.
#include "core/theory/triadcycle.h"
#include "fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace cc::theory;

namespace
{

const Tuning &standardTuning()
{
    static const Tuning tuning = Tuning::standardSix();
    return tuning;
}

struct FixtureSequence {
    std::string title;
    std::vector<std::vector<StringFret>> chords; // positions per chord, any order
};

std::vector<FixtureSequence> sequencesOf(const std::string &fixtureName)
{
    std::vector<FixtureSequence> out;
    for (const auto &seq : cc::test::loadRoutineFixture(fixtureName)["sequences"]) {
        FixtureSequence fs{seq["title"], {}};
        for (const auto &chord : seq["chords"]) {
            std::vector<StringFret> notes;
            for (const auto &note : chord["notes"]) {
                notes.push_back(StringFret{note[0].get<int>(), note[1].get<int>()});
            }
            fs.chords.push_back(notes);
        }
        out.push_back(fs);
    }
    return out;
}

std::set<std::pair<int, int>> asSet(const std::vector<StringFret> &positions)
{
    std::set<std::pair<int, int>> out;
    for (const StringFret &p : positions) {
        out.insert({p.string, p.fret});
    }
    return out;
}

std::set<std::pair<int, int>> asSet(const std::array<StringFret, 3> &positions)
{
    return asSet(std::vector<StringFret>(positions.begin(), positions.end()));
}

std::optional<StringSet> stringSetIn(const std::string &title)
{
    if (title.contains("1·2·3")) {
        return StringSet::Strings123;
    }
    if (title.contains("2·3·4")) {
        return StringSet::Strings234;
    }
    if (title.contains("3·4·5")) {
        return StringSet::Strings345;
    }
    if (title.contains("4·5·6")) {
        return StringSet::Strings456;
    }
    return std::nullopt;
}

std::optional<TriadCycle> cycleIn(const std::string &title)
{
    if (title.contains("Cycle 2")) {
        return TriadCycle::Seconds;
    }
    if (title.contains("Cycle 6") || title.contains("Cycle 3")) {
        return TriadCycle::Thirds;
    }
    if (title.contains("Cycle 4")) {
        return TriadCycle::Fourths;
    }
    return std::nullopt;
}

void requireSequenceEquals(const TriadSequence &generated, const FixtureSequence &expected)
{
    REQUIRE(expected.chords.size() == 8);
    for (std::size_t i = 0; i < 8; ++i) {
        INFO(expected.title << " chord " << i + 1);
        REQUIRE(asSet(generated[i].notes) == asSet(expected.chords[i]));
    }
}

std::set<int> pitchClassesOf(const TriadVoicing &voicing, const Tuning &tuning)
{
    std::set<int> out;
    for (const StringFret &n : voicing.notes) {
        REQUIRE(n.fret >= 0);
        REQUIRE(n.fret <= highestTriadFret);
        out.insert(tuning.pitchClassAt(n));
    }
    return out;
}

int notesMoved(const TriadVoicing &a, const TriadVoicing &b)
{
    int moved = 0;
    for (std::size_t s = 0; s < 3; ++s) {
        moved += a.notes[s] == b.notes[s] ? 0 : 1;
    }
    return moved;
}

std::set<int> chordTonesOf(const TriadVoicing &v)
{
    return {v.rootPitchClass,
            transpose(v.rootPitchClass, chordToneInterval(v.quality, ChordTone::Third)),
            transpose(v.rootPitchClass, chordToneInterval(v.quality, ChordTone::Fifth))};
}

/// Closed = bottom to top within an octave (section 4.3); the inversion names the bottom chord tone.
bool isClosedShape(const TriadVoicing &v, const Tuning &tuning)
{
    return tuning.pitchAt(v.notes[2]) - tuning.pitchAt(v.notes[0]) < semitonesPerOctave;
}

void requireValidSequence(const TriadSequence &sequence, const ClosedCycleOptions &options, const Tuning &tuning)
{
    const auto degrees = cycleDegrees(options.cycle, options.direction);
    for (std::size_t i = 0; i < 8; ++i) {
        INFO("slot " << i + 1);
        const TriadVoicing &v = sequence[i];
        REQUIRE(v.degree == degrees[i]);
        REQUIRE(pitchClassesOf(v, tuning) == chordTonesOf(v));
        REQUIRE(isClosedShape(v, tuning));
        if (i > 0) {
            REQUIRE(notesMoved(sequence[i], sequence[i - 1]) == movingNotesPerStep(options.cycle));
        }
    }
}

std::set<int> pitchesOf(const TriadVoicing &v, const Tuning &tuning)
{
    std::set<int> out;
    for (const StringFret &n : v.notes) {
        out.insert(tuning.pitchAt(n));
    }
    return out;
}

int commonPitches(const TriadVoicing &a, const TriadVoicing &b, const Tuning &tuning)
{
    const std::set<int> pa = pitchesOf(a, tuning);
    const std::set<int> pb = pitchesOf(b, tuning);
    return static_cast<int>(std::ranges::count_if(pa, [&pb](int p) {
        return pb.contains(p);
    }));
}

/// Open 3-6 (section 4.3): bottom on string 4, top on string 1, middle on 2 or 3, hand box of 4 frets, one voice
/// moves per step except at the rewind to the start chord (slot 7 -> 8 going up, slot 1 -> 2 going down).
void requireValidOpenThirds(const TriadSequence &sequence, Direction direction, const Tuning &tuning)
{
    const auto degrees = cycleDegrees(TriadCycle::Thirds, direction);
    const std::size_t rewind = direction == Direction::Up ? 7 : 1;
    for (std::size_t i = 0; i < 8; ++i) {
        INFO("slot " << i + 1);
        const TriadVoicing &v = sequence[i];
        REQUIRE(v.degree == degrees[i]);
        REQUIRE(pitchClassesOf(v, tuning) == chordTonesOf(v));
        REQUIRE(v.notes[0].string == 4);
        REQUIRE((v.notes[1].string == 2 || v.notes[1].string == 3));
        REQUIRE(v.notes[2].string == 1);
        const auto [low, high] = std::ranges::minmax(v.notes, {}, &StringFret::fret);
        REQUIRE(high.fret - low.fret < openHandBoxFrets);
        if (i > 0 && i != rewind) {
            REQUIRE(commonPitches(sequence[i], sequence[i - 1], tuning) == 2);
        }
    }
    REQUIRE(asSet(sequence[0].notes) == asSet(sequence[7].notes));
}

} // namespace

TEST_CASE("triads: closed cycles in C equal the source sequences (cycle 2 on four string sets, cycle 6, cycle 4)")
{
    int matched = 0;
    for (const FixtureSequence &fs : sequencesOf("closed-voicing")) {
        const auto strings = stringSetIn(fs.title);
        const auto cycle = cycleIn(fs.title);
        if (!strings || !cycle) {
            continue;
        }
        const ClosedCycleOptions options{*strings, Inversion::Auto, *cycle, Direction::Up};
        requireSequenceEquals(closedTriadCycle(0, options, standardTuning()), fs);
        ++matched;
    }
    REQUIRE(matched == 6);
}

TEST_CASE("triads: open cycle 2 on strings 5-4-2 in C equals the source sequence")
{
    int matched = 0;
    for (const FixtureSequence &fs : sequencesOf("open-voicing")) {
        if (!fs.title.contains("Cycle 2")) {
            continue;
        }
        requireSequenceEquals(openTriadCycle(0, TriadCycle::Seconds, Direction::Up, standardTuning()), fs);
        ++matched;
    }
    REQUIRE(matched == 1);
}

TEST_CASE("triads: open cycle 3-6 in C equals the source sequence (down = source order, up = the chain reversed)")
{
    // The source shows the one-voice chain in cycle-3 order, which section 4.1 maps to `down`; the chain itself
    // (C Am F Dm Bdim G Em, the fifth becoming the next root) is `up` (section 4.3).
    int matched = 0;
    for (const FixtureSequence &fs : sequencesOf("open-voicing")) {
        if (!fs.title.contains("Cycle 3")) {
            continue;
        }
        requireSequenceEquals(openTriadCycle(0, TriadCycle::Thirds, Direction::Down, standardTuning()), fs);
        FixtureSequence reversed = fs;
        std::ranges::reverse(reversed.chords);
        requireSequenceEquals(openTriadCycle(0, TriadCycle::Thirds, Direction::Up, standardTuning()), reversed);
        ++matched;
    }
    REQUIRE(matched == 1);
}

TEST_CASE("triads: open cycle 3-6 in 12 keys x 2 directions keeps chord tones, two common pitches and a 4-fret box")
{
    const Tuning &tuning = standardTuning();
    for (int key = 0; key < 12; ++key) {
        for (const Direction direction : {Direction::Up, Direction::Down}) {
            INFO("key " << key << " dir " << static_cast<int>(direction));
            requireValidOpenThirds(openTriadCycle(key, TriadCycle::Thirds, direction, tuning), direction, tuning);
        }
    }
}

TEST_CASE(
    "triads: 12 keys x 4 string sets x 4 inversions x 3 cycles x 2 directions stay closed, keep chord tones "
    "and move the right number of notes")
{
    const Tuning &tuning = standardTuning();
    for (int key = 0; key < 12; ++key) {
        for (const StringSet strings :
             {StringSet::Strings123, StringSet::Strings234, StringSet::Strings345, StringSet::Strings456}) {
            for (const Inversion inversion : {Inversion::Auto, Inversion::Root, Inversion::First, Inversion::Second}) {
                for (const TriadCycle cycle : {TriadCycle::Seconds, TriadCycle::Thirds, TriadCycle::Fourths}) {
                    for (const Direction direction : {Direction::Up, Direction::Down}) {
                        const ClosedCycleOptions options{strings, inversion, cycle, direction};
                        INFO("key " << key << " set " << static_cast<int>(strings) << " inv "
                                    << static_cast<int>(inversion) << " cycle " << static_cast<int>(cycle) << " dir "
                                    << static_cast<int>(direction));
                        requireValidSequence(closedTriadCycle(key, options, tuning), options, tuning);
                    }
                }
            }
        }
    }
}

TEST_CASE("triads: a manual inversion starts with that chord tone at the bottom, at the lowest closed position")
{
    const Tuning &tuning = standardTuning();
    const std::array<ChordTone, 3> bottomTone{ChordTone::Root, ChordTone::Third, ChordTone::Fifth};
    const std::array<Inversion, 3> inversions{Inversion::Root, Inversion::First, Inversion::Second};
    for (int key = 0; key < 12; ++key) {
        for (const StringSet strings :
             {StringSet::Strings123, StringSet::Strings234, StringSet::Strings345, StringSet::Strings456}) {
            for (std::size_t i = 0; i < 3; ++i) {
                const ClosedCycleOptions options{strings, inversions[i], TriadCycle::Seconds, Direction::Up};
                const TriadVoicing first = closedTriadCycle(key, options, tuning)[0];
                INFO("key " << key << " set " << static_cast<int>(strings) << " inv " << i);
                const PitchClass bottom = transpose(key, chordToneInterval(ChordQuality::Major, bottomTone[i]));
                REQUIRE(tuning.pitchClassAt(first.notes[0]) == bottom);
                const int lowestFret = intervalUp(tuning.pitchClassAt({first.notes[0].string, 0}), bottom);
                REQUIRE(first.notes[0].fret - lowestFret <= semitonesPerOctave); // an octave up only when needed
            }
        }
    }
    // C on 1-2-3 (bottom string first): root 5 5 3, first 9 8 8, second 12 13 12 (0 1 0 would use open strings,
    // which a start voicing never does: minStartFret).
    const auto tabOf = [&tuning](Inversion inversion) {
        const ClosedCycleOptions options{StringSet::Strings123, inversion, TriadCycle::Seconds, Direction::Up};
        return closedTriadCycle(0, options, tuning)[0].notes;
    };
    REQUIRE(tabOf(Inversion::Root) == std::array<StringFret, 3>{{{3, 5}, {2, 5}, {1, 3}}});
    REQUIRE(tabOf(Inversion::First) == std::array<StringFret, 3>{{{3, 9}, {2, 8}, {1, 8}}});
    REQUIRE(tabOf(Inversion::Second) == std::array<StringFret, 3>{{{3, 12}, {2, 13}, {1, 12}}});
    // G root on 1-2-3: D4 lies below the open E string, so the closed shape starts an octave up (12 12 10)
    // instead of the spread 0 0 10.
    const ClosedCycleOptions gRoot{StringSet::Strings123, Inversion::Auto, TriadCycle::Seconds, Direction::Up};
    REQUIRE(closedTriadCycle(7, gRoot, tuning)[0].notes == std::array<StringFret, 3>{{{3, 12}, {2, 12}, {1, 10}}});
}

TEST_CASE("triads: no start voicing uses an open string (closed 12 keys x 4 sets x 4 inversions, open 3-6 and 2)")
{
    const Tuning &tuning = standardTuning();
    const auto requireFretted = [](const TriadVoicing &first) {
        for (const StringFret &note : first.notes) {
            REQUIRE(note.fret >= minStartFret);
        }
    };
    for (int key = 0; key < 12; ++key) {
        for (const StringSet strings :
             {StringSet::Strings123, StringSet::Strings234, StringSet::Strings345, StringSet::Strings456}) {
            for (const Inversion inversion : {Inversion::Auto, Inversion::Root, Inversion::First, Inversion::Second}) {
                const ClosedCycleOptions options{strings, inversion, TriadCycle::Seconds, Direction::Up};
                INFO("key " << key << " set " << static_cast<int>(strings) << " inv " << static_cast<int>(inversion));
                requireFretted(closedTriadCycle(key, options, tuning)[0]);
            }
        }
        INFO("open key " << key);
        requireFretted(openTriadCycle(key, TriadCycle::Thirds, Direction::Up, tuning)[0]);
        requireFretted(openTriadCycle(key, TriadCycle::Seconds, Direction::Up, tuning)[0]);
    }
    // G open 3-6 would be x x 0 x 0 3 (D B G on strings 4-2-1); it starts an octave up instead.
    const TriadSequence g = openTriadCycle(7, TriadCycle::Thirds, Direction::Up, tuning);
    REQUIRE(g[0].notes == std::array<StringFret, 3>{{{4, 12}, {2, 12}, {1, 15}}});
}

TEST_CASE("triads: a cycle that climbs past the fretboard drops the last slot an octave (E major, strings 3-4-5)")
{
    const ClosedCycleOptions options{StringSet::Strings345, Inversion::Auto, TriadCycle::Seconds, Direction::Up};
    const auto sequence = closedTriadCycle(4, options, standardTuning());
    REQUIRE(sequence[0].notes[0].fret == 11); // G# on the A string
    REQUIRE(sequence[7].notes[0].fret == 11); // the closing E chord would sit at 23, so it returns to the start
}
