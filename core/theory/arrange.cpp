// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/theory/arrange.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace cc::theory
{

namespace
{

constexpr int maxRun = 4; // notes on one string the Diagonal state remembers (the longest group)
constexpr int positionSpan = 4; // frets a hand covers in one position (Vertical)
constexpr double infinite = std::numeric_limits<double>::infinity();

/// What a pattern prices (fingering.md section 5; values tuned on the routine's diagonal forms and a
/// two-octave sweep, arrange_test.cpp).
struct Costs {
    double shift; // per fret of position change (Horizontal, Diagonal) or of window move (Vertical)
    double bigShift; // added when a change exceeds bigShiftFrets
    int bigShiftFrets;
    double stringChange; // per string change
    double stringSkip; // per string skipped over
    double earlyChange; // Diagonal: leaving a string after fewer than three notes
    double longRun; // Diagonal: a fifth note on a string
    double sameGroup; // Diagonal: two strings in a row with the same group size (the form alternates 3 / 4)
    double highFret; // per fret above the twelfth
};

constexpr Costs costsOf(FingeringPattern pattern)
{
    switch (pattern) {
    case FingeringPattern::Vertical:
        return {3.0, 0.0, 99, 0.5, 1.0, 0.0, 0.0, 0.0, 0.1};
    case FingeringPattern::Horizontal:
        return {1.0, 0.0, 99, 5.0, 2.0, 0.0, 0.0, 0.0, 0.1};
    case FingeringPattern::Diagonal:
        return {1.0, 4.0, 4, 0.5, 2.0, 3.0, 4.0, 2.0, 0.05};
    }
    return {1.0, 0.0, 99, 1.0, 1.0, 0.0, 0.0, 0.0, 0.1};
}

/// A DP state: which candidate, and what the pattern needs to remember. Diagonal keeps the run on the current
/// string and the previous string's group size; Vertical keeps the position window's lowest fret.
struct Dims {
    int runs = 1; // 1 or maxRun
    int lastRuns = 1; // 1 or 3 (unknown / three / four)
    int anchors = 1; // 1 or maxFret + 1

    [[nodiscard]] std::size_t perCandidate() const
    {
        return static_cast<std::size_t>(runs) * static_cast<std::size_t>(lastRuns) * static_cast<std::size_t>(anchors);
    }
};

struct State {
    std::size_t candidate = 0;
    int run = 1; // notes played on the current string, 1..maxRun
    int lastRun = 0; // 0 unknown, 3 or 4: the group size the previous string ended with
    int anchor = 0; // Vertical: lowest fret of the position window
};

Dims dimsOf(FingeringPattern pattern, int maxFret)
{
    Dims dims;
    if (pattern == FingeringPattern::Diagonal) {
        dims.runs = maxRun;
        dims.lastRuns = 3;
    } else if (pattern == FingeringPattern::Vertical) {
        dims.anchors = maxFret + 1;
    }
    return dims;
}

std::size_t flatIndex(const Dims &dims, const State &s)
{
    const int lastIndex = s.lastRun == 0 ? 0 : s.lastRun - 2; // 0, 1 (three), 2 (four)
    const std::size_t runAndLast = (static_cast<std::size_t>(s.run - 1) * static_cast<std::size_t>(dims.lastRuns))
        + static_cast<std::size_t>(lastIndex);
    const std::size_t inner =
        (runAndLast * static_cast<std::size_t>(dims.anchors)) + static_cast<std::size_t>(s.anchor);
    return (s.candidate * dims.perCandidate()) + inner;
}

State stateAt(const Dims &dims, std::size_t flat)
{
    State s;
    s.candidate = flat / dims.perCandidate();
    std::size_t inner = flat % dims.perCandidate();
    s.anchor = static_cast<int>(inner % static_cast<std::size_t>(dims.anchors));
    inner /= static_cast<std::size_t>(dims.anchors);
    const int lastIndex = static_cast<int>(inner % static_cast<std::size_t>(dims.lastRuns));
    s.lastRun = lastIndex == 0 ? 0 : lastIndex + 2;
    s.run = static_cast<int>(inner / static_cast<std::size_t>(dims.lastRuns)) + 1;
    return s;
}

/// Every (string, fret) that sounds the pitch; a pin that is among them is the only candidate, a pin that
/// is not (it cannot sound the pitch) is ignored.
std::vector<StringFret>
candidatesOf(Midi midi, const Tuning &tuning, const ArrangeOptions &options, const std::optional<StringFret> &pin)
{
    std::vector<StringFret> all;
    for (int string = 1; string <= tuning.stringCount(); ++string) {
        const int fret = midi - tuning.pitchAt({string, 0});
        if (fret >= 0 && fret <= options.maxFret && (fret != 0 || options.allowOpen)) {
            all.push_back({string, fret});
        }
    }
    if (pin && std::ranges::find(all, *pin) != all.end()) {
        return {*pin};
    }
    return all;
}

/// Where the window goes to take `fret` in, and how far it had to move.
std::pair<int, int> windowFor(int anchor, int fret)
{
    if (fret == 0) {
        return {anchor, 0}; // an open string floats
    }
    if (fret < anchor) {
        return {fret, anchor - fret};
    }
    if (fret > anchor + positionSpan) {
        return {fret - positionSpan, fret - (anchor + positionSpan)};
    }
    return {anchor, 0};
}

class Arranger
{
public:
    Arranger(std::span<const Midi> notes,
             const Tuning &tuning,
             const ArrangeOptions &options,
             std::span<const std::optional<StringFret>> pins)
        : m_pattern(options.pattern)
        , m_costs(costsOf(options.pattern))
        , m_dims(dimsOf(options.pattern, options.maxFret))
    {
        m_candidates.reserve(notes.size());
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const std::optional<StringFret> pin = i < pins.size() ? pins[i] : std::nullopt;
            m_candidates.push_back(candidatesOf(notes[i], tuning, options, pin));
        }
    }

    [[nodiscard]] std::vector<std::optional<StringFret>> solve() const
    {
        std::vector<std::optional<StringFret>> out(m_candidates.size());
        std::size_t start = 0;
        while (start < m_candidates.size()) {
            if (m_candidates[start].empty()) {
                ++start;
                continue;
            }
            std::size_t end = start;
            while (end < m_candidates.size() && !m_candidates[end].empty()) {
                ++end;
            }
            solveStretch(start, end, out);
            start = end;
        }
        return out;
    }

private:
    struct Cell {
        double cost = infinite;
        std::size_t prev = 0; // flat state of the previous note
    };

    /// Forward pass over notes [from, to), then the back pointers give the positions.
    void solveStretch(std::size_t from, std::size_t to, std::vector<std::optional<StringFret>> &out) const
    {
        std::vector<std::vector<Cell>> table(to - from);
        for (std::size_t i = from; i < to; ++i) {
            table[i - from].assign(m_candidates[i].size() * m_dims.perCandidate(), Cell{});
            if (i == from) {
                seed(i, table[0]);
            } else {
                relax(i, table[i - from - 1], table[i - from]);
            }
        }
        std::size_t best = 0;
        const std::vector<Cell> &last = table.back();
        for (std::size_t s = 1; s < last.size(); ++s) {
            if (last[s].cost < last[best].cost) {
                best = s;
            }
        }
        for (std::size_t i = to; i-- > from;) {
            out[i] = m_candidates[i][stateAt(m_dims, best).candidate];
            best = table[i - from][best].prev;
        }
    }

    /// The first note: every candidate at run 1, unknown last group, and (Vertical) every window that holds it.
    void seed(std::size_t i, std::vector<Cell> &row) const
    {
        for (std::size_t c = 0; c < m_candidates[i].size(); ++c) {
            const StringFret to = m_candidates[i][c];
            seedCandidate(c, to.fret, m_costs.highFret * std::max(0, to.fret - 12), row);
        }
    }

    void seedCandidate(std::size_t c, int fret, double cost, std::vector<Cell> &row) const
    {
        for (int anchor = 0; anchor < m_dims.anchors; ++anchor) {
            if (windowHolds(anchor, fret)) {
                row[flatIndex(m_dims, State{c, 1, 0, anchor})].cost = cost;
            }
        }
    }

    [[nodiscard]] bool windowHolds(int anchor, int fret) const
    {
        return m_dims.anchors == 1 || fret == 0 || (fret >= anchor && fret <= anchor + positionSpan);
    }

    /// Every live state of the previous note into the state each candidate of this note leads to.
    void relax(std::size_t i, const std::vector<Cell> &previous, std::vector<Cell> &row) const
    {
        for (std::size_t p = 0; p < previous.size(); ++p) {
            if (previous[p].cost != infinite) {
                relaxFrom(i, p, previous[p].cost, row);
            }
        }
    }

    void relaxFrom(std::size_t i, std::size_t p, double soFar, std::vector<Cell> &row) const
    {
        const State fromState = stateAt(m_dims, p);
        const StringFret fromPos = m_candidates[i - 1][fromState.candidate];
        for (std::size_t c = 0; c < m_candidates[i].size(); ++c) {
            const auto [next, cost] = step(fromState, fromPos, c, m_candidates[i][c]);
            Cell &target = row[flatIndex(m_dims, next)];
            if (soFar + cost < target.cost) {
                target = Cell{soFar + cost, p};
            }
        }
    }

    /// The state after playing `to` from `fromState`, and what the move costs.
    [[nodiscard]] std::pair<State, double>
    step(const State &fromState, const StringFret &from, std::size_t candidate, const StringFret &to) const
    {
        State next{candidate, 1, fromState.lastRun, fromState.anchor};
        double cost = m_costs.highFret * std::max(0, to.fret - 12);
        if (m_pattern == FingeringPattern::Vertical) {
            const auto [anchor, moved] = windowFor(fromState.anchor, to.fret);
            next.anchor = anchor;
            cost += m_costs.shift * moved;
        } else {
            const int shift = std::abs(to.fret - from.fret); // an open string counts as fret 0: a real move
            cost += (m_costs.shift * shift) + (shift > m_costs.bigShiftFrets ? m_costs.bigShift : 0.0);
        }
        if (to.string == from.string) {
            next.run = std::min(m_dims.runs, fromState.run + 1);
            cost += fromState.run >= maxRun ? m_costs.longRun : 0.0;
        } else {
            cost += stringChangeCost(fromState, from, to);
            next.lastRun = m_dims.lastRuns > 1 ? std::clamp(fromState.run, 3, 4) : 0;
        }
        return {next, cost};
    }

    [[nodiscard]] double stringChangeCost(const State &fromState, const StringFret &from, const StringFret &to) const
    {
        double cost = m_costs.stringChange + (m_costs.stringSkip * (std::abs(to.string - from.string) - 1));
        if (fromState.run < 3) {
            cost += m_costs.earlyChange;
        }
        if (fromState.lastRun != 0 && fromState.lastRun == std::clamp(fromState.run, 3, 4)) {
            cost += m_costs.sameGroup;
        }
        return cost;
    }

    FingeringPattern m_pattern;
    Costs m_costs;
    Dims m_dims;
    std::vector<std::vector<StringFret>> m_candidates;
};

} // namespace

std::vector<std::optional<StringFret>> arrangeFingering(std::span<const Midi> notes,
                                                        const Tuning &tuning,
                                                        const ArrangeOptions &options,
                                                        std::span<const std::optional<StringFret>> pins)
{
    return Arranger(notes, tuning, options, pins).solve();
}

} // namespace cc::theory
