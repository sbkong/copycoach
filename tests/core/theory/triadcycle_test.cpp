// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// routine-spec.md section 4.6 completion criteria for module 03.
#include "core/theory/triadcycle.h"
#include "fixtures.h"

#include <catch2/catch_test_macros.hpp>

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

void requireValidSequence(const TriadSequence &sequence, const ClosedCycleOptions &options, const Tuning &tuning)
{
    const auto degrees = cycleDegrees(options.cycle, options.direction);
    for (std::size_t i = 0; i < 8; ++i) {
        INFO("slot " << i + 1);
        const TriadVoicing &v = sequence[i];
        REQUIRE(v.degree == degrees[i]);
        const std::set<int> expected{v.rootPitchClass,
                                     transpose(v.rootPitchClass, chordToneInterval(v.quality, ChordTone::Third)),
                                     transpose(v.rootPitchClass, chordToneInterval(v.quality, ChordTone::Fifth))};
        REQUIRE(pitchClassesOf(v, tuning) == expected);
        if (i > 0) {
            REQUIRE(notesMoved(sequence[i], sequence[i - 1]) == movingNotesPerStep(options.cycle));
        }
    }
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

TEST_CASE(
    "triads: 12 keys x 4 string sets x 3 cycles x 2 directions keep chord tones and move the right number of notes")
{
    const Tuning &tuning = standardTuning();
    for (int key = 0; key < 12; ++key) {
        for (const StringSet strings :
             {StringSet::Strings123, StringSet::Strings234, StringSet::Strings345, StringSet::Strings456}) {
            for (const TriadCycle cycle : {TriadCycle::Seconds, TriadCycle::Thirds, TriadCycle::Fourths}) {
                for (const Direction direction : {Direction::Up, Direction::Down}) {
                    const ClosedCycleOptions options{strings, Inversion::Auto, cycle, direction};
                    INFO("key " << key << " set " << static_cast<int>(strings) << " cycle " << static_cast<int>(cycle)
                                << " dir " << static_cast<int>(direction));
                    requireValidSequence(closedTriadCycle(key, options, tuning), options, tuning);
                }
            }
        }
    }
}

TEST_CASE("triads: a cycle that climbs past the fretboard drops the last slot an octave (E major, strings 3-4-5)")
{
    const ClosedCycleOptions options{StringSet::Strings345, Inversion::Auto, TriadCycle::Seconds, Direction::Up};
    const auto sequence = closedTriadCycle(4, options, standardTuning());
    REQUIRE(sequence[0].notes[0].fret == 11); // G# on the A string
    REQUIRE(sequence[7].notes[0].fret == 11); // the closing E chord would sit at 23, so it returns to the start
}
