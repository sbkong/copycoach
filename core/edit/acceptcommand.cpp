// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/commands.h"
#include "core/edit/measureops.h"
#include "core/edit/snapshot.h"

#include <cassert>

// copy.predict.accept: moves predicted notes or chord symbols of [measureFrom, measureTo] into the score.
namespace cc::edit
{

namespace
{

/// Copies of measures [from, to] with their absolute start ticks, so items find their measure by tick.
struct Window {
    const model::Score *score;
    std::size_t from;
    std::size_t to;
    std::vector<model::Measure> measures;
    std::vector<model::Tick> starts; // starts[i] = first tick of measure from + i; one extra for the end
    bool changed = false;

    Window(const model::Score &s, std::size_t f, std::size_t t)
        : score(&s)
        , from(f)
        , to(t)
        , measures(s.measures.begin() + static_cast<std::ptrdiff_t>(f),
                   s.measures.begin() + static_cast<std::ptrdiff_t>(t + 1))
    {
        model::Tick tick = s.measureStartTick(f);
        for (std::size_t i = f; i <= t; ++i) {
            starts.push_back(tick);
            tick += model::measureTicks(s.timeSigAt(i));
        }
        starts.push_back(tick);
    }

    /// The window measure holding an absolute tick, or nullptr outside the window.
    model::Measure *measureFor(model::Tick tick)
    {
        for (std::size_t i = 0; i < measures.size(); ++i) {
            if (tick >= starts[i] && tick < starts[i + 1]) {
                return &measures[i];
            }
        }
        return nullptr;
    }

    /// The beat position of an absolute tick inside the window measure `measure`.
    BeatRef refFor(const model::Measure *measure, std::size_t track, model::Tick tick) const
    {
        const auto i = static_cast<std::size_t>(measure - measures.data());
        return BeatRef{from + i, track, 1, tick - starts[i]};
    }
};

void acceptNotes(Window &window, const model::Prediction &prediction, std::size_t track, model::IdGenerator &ids)
{
    for (const model::PredictedItem &item : prediction.items) {
        if (!item.position || !ops::stringPlayable(*window.score, track, item.position->string)
            || item.position->fret < 0 || item.position->fret > maxFret) {
            continue;
        }
        model::Measure *measure = window.measureFor(item.tick);
        if (measure == nullptr) {
            continue;
        }
        const BeatRef at = window.refFor(measure, track, item.tick);
        const model::Dur dur = ops::nearestDur(item.dur);
        window.changed |= ops::putNote(*measure, at, *item.position, dur, ids);
    }
}

void acceptChords(Window &window, const model::Prediction &prediction, model::IdGenerator &ids)
{
    for (const model::PredictedItem &item : prediction.items) {
        if (item.text.empty()) {
            continue;
        }
        model::Measure *measure = window.measureFor(item.tick);
        if (measure == nullptr) {
            continue;
        }
        const BeatRef at = window.refFor(measure, 0, item.tick);
        window.changed |= ops::putChord(*measure, at.tick, item.text, ids);
    }
}

} // namespace

std::unique_ptr<Command> acceptPrediction(const project::Project &project,
                                          const model::Prediction &prediction,
                                          MeasureSpan measures,
                                          std::size_t track,
                                          model::IdGenerator &ids)
{
    const model::Score &score = project.score;
    assert(measures.from <= measures.to && measures.to < score.measures.size());
    Window window(score, measures.from, measures.to);
    switch (prediction.kind) {
    case model::PredictionKind::Notes:
        acceptNotes(window, prediction, track, ids);
        break;
    case model::PredictionKind::Chords:
        acceptChords(window, prediction, ids);
        break;
    case model::PredictionKind::Beats:
    case model::PredictionKind::Key:
    case model::PredictionKind::Fingering:
        break; // no accept path yet (commands.h)
    }
    if (!window.changed) {
        return nullptr;
    }
    return std::make_unique<MeasureEdit>(measures.from, std::move(window.measures), false);
}

} // namespace cc::edit
