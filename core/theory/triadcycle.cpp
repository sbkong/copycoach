// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/triadcycle.h"

#include <algorithm>
#include <cassert>
#include <limits>

namespace cc::theory
{

namespace
{

constexpr std::array<int, 8> secondsUp{1, 2, 3, 4, 5, 6, 7, 1};
constexpr std::array<int, 8> thirdsUp{1, 6, 4, 2, 7, 5, 3, 1};
constexpr std::array<int, 8> fourthsUp{1, 4, 7, 3, 6, 2, 5, 1};
using Permutation = std::array<std::size_t, 3>;
constexpr std::array<Permutation, 6> permutations{Permutation{0, 1, 2},
                                                  Permutation{0, 2, 1},
                                                  Permutation{1, 0, 2},
                                                  Permutation{1, 2, 0},
                                                  Permutation{2, 0, 1},
                                                  Permutation{2, 1, 0}};

std::array<int, 3> stringsOf(StringSet set) // bottom first
{
    switch (set) {
    case StringSet::Strings123:
        return {3, 2, 1};
    case StringSet::Strings234:
        return {4, 3, 2};
    case StringSet::Strings345:
        return {5, 4, 3};
    case StringSet::Strings456:
        return {6, 5, 4};
    }
    return {3, 2, 1};
}

Inversion autoInversion(StringSet set)
{
    switch (set) {
    case StringSet::Strings123:
        return Inversion::Root;
    case StringSet::Strings234:
        return Inversion::Second;
    case StringSet::Strings345:
        return Inversion::First;
    case StringSet::Strings456:
        return Inversion::Second;
    }
    return Inversion::Root;
}

/// Chord tones bottom to top for an inversion.
std::array<ChordTone, 3> tonesBottomUp(Inversion inversion)
{
    switch (inversion) {
    case Inversion::First:
        return {ChordTone::Third, ChordTone::Fifth, ChordTone::Root};
    case Inversion::Second:
        return {ChordTone::Fifth, ChordTone::Root, ChordTone::Third};
    case Inversion::Root:
    case Inversion::Auto:
        return {ChordTone::Root, ChordTone::Third, ChordTone::Fifth};
    }
    return {ChordTone::Root, ChordTone::Third, ChordTone::Fifth};
}

std::array<PitchClass, 3> chordPitchClasses(PitchClass root, ChordQuality quality)
{
    return {root,
            transpose(root, chordToneInterval(quality, ChordTone::Third)),
            transpose(root, chordToneInterval(quality, ChordTone::Fifth))};
}

/// First slot: tones stacked bottom to top, each at the lowest fret above the previous note.
TriadVoicing startVoicing(PitchClass key,
                          const std::array<int, 3> &strings,
                          const std::array<ChordTone, 3> &tones,
                          const Tuning &tuning)
{
    const ChordQuality quality = diatonicTriad(1);
    TriadVoicing voicing{1, key, quality, {}};
    Midi previous = -1;
    for (std::size_t i = 0; i < 3; ++i) {
        const PitchClass pitchClass = transpose(key, chordToneInterval(quality, tones[i]));
        int fret = intervalUp(pitchClassOf(tuning.openPitch(strings[i]) + tuning.capo), pitchClass);
        while (tuning.pitchAt({strings[i], fret}) <= previous) {
            fret += semitonesPerOctave;
        }
        voicing.notes[i] = StringFret{strings[i], fret};
        previous = tuning.pitchAt(voicing.notes[i]);
    }
    return voicing;
}

/// Marks which target tones are already sounding (they stay) and which strings therefore have to move.
struct CommonTones {
    std::array<bool, 3> toneUsed{};
    std::array<bool, 3> stringMoves{};
};

CommonTones findCommonTones(const TriadVoicing &current, const std::array<PitchClass, 3> &target, const Tuning &tuning)
{
    CommonTones result;
    for (std::size_t s = 0; s < 3; ++s) {
        const PitchClass pc = tuning.pitchClassAt(current.notes[s]);
        const auto it = std::ranges::find(target, pc);
        const auto index = static_cast<std::size_t>(it - target.begin());
        if (it != target.end() && !result.toneUsed[index]) {
            result.toneUsed[index] = true;
        } else {
            result.stringMoves[s] = true;
        }
    }
    return result;
}

/// Moves every non-common string up to the tone the permutation assigns it; returns the total motion (or nullopt
/// when the permutation does not line up with the free tones).
struct Assignment {
    std::array<StringFret, 3> notes;
    int cost;
};

Assignment assign(const TriadVoicing &current,
                  const std::array<PitchClass, 3> &target,
                  const CommonTones &common,
                  const Permutation &order,
                  const Tuning &tuning)
{
    Assignment result{current.notes, 0};
    std::size_t toneIndex = 0;
    for (std::size_t s = 0; s < 3; ++s) {
        if (!common.stringMoves[s]) {
            continue;
        }
        while (toneIndex < 3 && common.toneUsed[order[toneIndex]]) {
            ++toneIndex;
        }
        if (toneIndex >= 3) {
            return Assignment{current.notes, std::numeric_limits<int>::max()};
        }
        const int step = intervalUp(tuning.pitchClassAt(current.notes[s]), target[order[toneIndex]]);
        result.notes[s].fret += step;
        result.cost += step;
        ++toneIndex;
    }
    return result;
}

/// Next slot: common tones stay; the other strings take the remaining tones with the least total upward motion.
TriadVoicing nextVoicing(const TriadVoicing &current, int degree, PitchClass key, const Tuning &tuning)
{
    const PitchClass root = degreePitchClass(key, degree);
    const ChordQuality quality = diatonicTriad(degree);
    const auto target = chordPitchClasses(root, quality);
    const CommonTones common = findCommonTones(current, target, tuning);
    Assignment best{current.notes, std::numeric_limits<int>::max()};
    for (const Permutation &order : permutations) {
        const Assignment candidate = assign(current, target, common, order, tuning);
        if (candidate.cost < best.cost) {
            best = candidate;
        }
    }
    return TriadVoicing{degree, root, quality, best.notes};
}

/// A slot that climbed past the top of the fretboard sounds the same chord an octave lower.
void dropIntoRange(TriadVoicing &voicing)
{
    const bool tooHigh = std::ranges::any_of(voicing.notes, [](const StringFret &n) {
        return n.fret > highestTriadFret;
    });
    if (tooHigh) {
        for (StringFret &note : voicing.notes) {
            note.fret -= semitonesPerOctave;
        }
    }
}

TriadSequence
runCycle(PitchClass key, const TriadVoicing &start, TriadCycle cycle, Direction direction, const Tuning &tuning)
{
    const auto degrees = cycleDegrees(cycle, Direction::Up);
    TriadSequence sequence{};
    sequence[0] = start;
    for (std::size_t i = 1; i < degrees.size(); ++i) {
        sequence[i] = nextVoicing(sequence[i - 1], degrees[i], key, tuning);
        dropIntoRange(sequence[i]);
    }
    if (direction == Direction::Down) {
        std::ranges::reverse(sequence);
    }
    return sequence;
}

std::array<int, 8> upDegrees(TriadCycle cycle)
{
    switch (cycle) {
    case TriadCycle::Seconds:
        return secondsUp;
    case TriadCycle::Thirds:
        return thirdsUp;
    case TriadCycle::Fourths:
        return fourthsUp;
    }
    return secondsUp;
}

} // namespace

std::array<int, 8> cycleDegrees(TriadCycle cycle, Direction direction)
{
    std::array<int, 8> degrees = upDegrees(cycle);
    if (direction == Direction::Down) {
        std::ranges::reverse(degrees);
    }
    return degrees;
}

int movingNotesPerStep(TriadCycle cycle)
{
    switch (cycle) {
    case TriadCycle::Seconds:
        return 3;
    case TriadCycle::Thirds:
        return 1;
    case TriadCycle::Fourths:
        return 2;
    }
    return 3;
}

TriadSequence closedTriadCycle(PitchClass key, const ClosedCycleOptions &options, const Tuning &tuning)
{
    const Inversion chosen = options.inversion == Inversion::Auto ? autoInversion(options.strings) : options.inversion;
    const TriadVoicing start = startVoicing(key, stringsOf(options.strings), tonesBottomUp(chosen), tuning);
    return runCycle(key, start, options.cycle, options.direction, tuning);
}

TriadSequence openTriadCycle(PitchClass key, TriadCycle cycle, Direction direction, const Tuning &tuning)
{
    assert(cycle == TriadCycle::Seconds); // thirds with string re-assignment: routine-spec.md section 4.3, not yet
    const TriadVoicing start =
        startVoicing(key, {5, 4, 2}, {ChordTone::Root, ChordTone::Fifth, ChordTone::Third}, tuning);
    return runCycle(key, start, cycle, direction, tuning);
}

} // namespace cc::theory
