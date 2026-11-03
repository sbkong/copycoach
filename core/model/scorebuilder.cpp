// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/model/scorebuilder.h"

#include <cassert>

namespace cc::model
{

namespace
{

Beat makeBeat(const BeatSpec &spec, Tick tick, IdGenerator &ids)
{
    Beat beat;
    beat.id = ids.next();
    beat.tick = tick;
    beat.dur = spec.dur;
    beat.rest = spec.notes.empty();
    for (const theory::StringFret &position : spec.notes) {
        beat.notes.push_back(Note{ids.next(), position.string, position.fret, spec.tieToNext});
    }
    return beat;
}

} // namespace

Score buildScore(const ScoreSpec &spec, IdGenerator &ids)
{
    Score score;
    score.tracks.push_back(Track{ids.next(), "Guitar", spec.tuning, TrackRole::Other});
    score.initialTimeSig = spec.timeSig;
    score.initialTempoBpm = spec.tempoBpm;
    const Tick capacity = measureTicks(spec.timeSig);
    for (const MeasureSpec &measureSpec : spec.measures) {
        Measure measure;
        measure.id = ids.next();
        Tick tick = 0;
        for (const BeatSpec &beatSpec : measureSpec.beats) {
            measure.beats.push_back(makeBeat(beatSpec, tick, ids));
            tick += measure.beats.back().ticks();
        }
        assert(tick <= capacity); // the spec author packs beats into measures; overflow is a programmer error
        score.measures.push_back(std::move(measure));
    }
    return score;
}

} // namespace cc::model
