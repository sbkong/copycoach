// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/commands.h"
#include "core/edit/snapshot.h"
#include "core/io/limits.h"

#include <algorithm>
#include <cassert>

// Commands that change the measure list or the score's non-measure parts (sections, tracks). Insert and delete
// carry every measure- or tick-anchored reference along (commands.h).
namespace cc::edit
{

namespace
{

project::Project shellOf(const project::Project &project)
{
    project::Project shell = project;
    shell.score.measures.clear();
    return shell;
}

/// The meter the measures inserted before `at` take: the one in force just before that point.
model::TimeSignature meterBefore(const model::Score &score, std::size_t at)
{
    return at == 0 ? score.initialTimeSig : score.timeSigAt(at - 1);
}

void shiftForInsert(project::Project &shell, std::size_t at, std::size_t count, model::Tick atTick, model::Tick ticks)
{
    for (model::Section &section : shell.score.sections) {
        if (section.startMeasure >= at) {
            section.startMeasure += count;
        }
    }
    for (project::Range &range : shell.ranges) {
        if (range.barFrom >= at) {
            range.barFrom += count;
        }
        if (range.barTo >= at) {
            range.barTo += count;
        }
    }
    for (project::SourceSyncPoint &point : shell.syncPoints) {
        if (point.point.tick >= atTick) {
            point.point.tick += ticks;
        }
    }
    if (shell.lastSession.playheadTick >= atTick) {
        shell.lastSession.playheadTick += ticks;
    }
}

void forgetRange(project::Project &shell, const model::Id &rangeId)
{
    shell.memos.ranges.erase(rangeId);
    for (project::Phrase &phrase : shell.phrases) {
        if (phrase.rangeId == rangeId) {
            phrase.rangeId.reset();
        }
    }
    if (shell.lastSession.rangeId == rangeId) {
        shell.lastSession.rangeId.reset();
    }
}

/// Clips a range around the deleted span [at, at + count); false when nothing of it survives.
bool clipRange(project::Range &range, std::size_t at, std::size_t count)
{
    const std::size_t end = at + count;
    if (range.barFrom >= at && range.barTo < end) {
        return false;
    }
    if (range.barFrom >= end) {
        range.barFrom -= count;
    } else if (range.barFrom >= at) {
        range.barFrom = at;
    }
    if (range.barTo >= end) {
        range.barTo -= count;
    } else if (range.barTo >= at) {
        range.barTo = at - 1; // barFrom < at here, so the range keeps at least one bar
    }
    return true;
}

void shiftForDelete(project::Project &shell, std::size_t at, std::size_t count, model::Tick from, model::Tick to)
{
    std::erase_if(shell.score.sections, [at, count](const model::Section &section) {
        return section.startMeasure >= at && section.startMeasure < at + count;
    });
    for (model::Section &section : shell.score.sections) {
        if (section.startMeasure >= at + count) {
            section.startMeasure -= count;
        }
    }
    std::vector<project::Range> kept;
    for (project::Range range : shell.ranges) {
        if (clipRange(range, at, count)) {
            kept.push_back(std::move(range));
        } else {
            forgetRange(shell, range.id);
        }
    }
    shell.ranges = std::move(kept);
    std::erase_if(shell.syncPoints, [from, to](const project::SourceSyncPoint &point) {
        return point.point.tick >= from && point.point.tick < to;
    });
    for (project::SourceSyncPoint &point : shell.syncPoints) {
        if (point.point.tick >= to) {
            point.point.tick -= to - from;
        }
    }
    auto &playhead = shell.lastSession.playheadTick;
    playhead = playhead >= to ? playhead - (to - from) : std::min(playhead, from);
}

/// The first surviving measure after a deleted span inherits the changes the span carried, so what follows
/// keeps its meter, key, tempo and swing.
std::vector<model::Measure> carriedSurvivor(const model::Score &score, std::size_t at, std::size_t count)
{
    const std::size_t next = at + count;
    if (next >= score.measures.size()) {
        return {};
    }
    model::Measure survivor = score.measures[next];
    const auto carry = [&](auto field, auto before, auto current) {
        if (!(survivor.*field) && !(before == current)) {
            survivor.*field = current;
        }
    };
    const std::size_t last = next - 1;
    carry(&model::Measure::timeSig, meterBefore(score, at), score.timeSigAt(last));
    carry(&model::Measure::keySig, at == 0 ? score.initialKeySig : score.keySigAt(at - 1), score.keySigAt(last));
    carry(&model::Measure::tempoBpm, at == 0 ? score.initialTempoBpm : score.tempoAt(at - 1), score.tempoAt(last));
    carry(&model::Measure::swingRatio, at == 0 ? score.initialSwingRatio : score.swingAt(at - 1), score.swingAt(last));
    return {survivor};
}

} // namespace

std::unique_ptr<Command>
insertMeasures(const project::Project &project, std::size_t at, std::size_t count, model::IdGenerator &ids)
{
    const model::Score &score = project.score;
    assert(at <= score.measures.size());
    if (count == 0 || score.measures.size() + count > io::maxMeasures) {
        return nullptr;
    }
    std::vector<model::Measure> fresh(count);
    for (model::Measure &measure : fresh) {
        measure.id = ids.next();
    }
    project::Project shell = shellOf(project);
    const model::Tick atTick = score.measureStartTick(at);
    const model::Tick ticks = static_cast<model::Tick>(count) * model::measureTicks(meterBefore(score, at));
    shiftForInsert(shell, at, count, atTick, ticks);
    return std::make_unique<ShellEdit>(std::move(shell), at, 0, std::move(fresh), DirtyRange::all(true));
}

std::unique_ptr<Command> deleteMeasures(const project::Project &project, std::size_t at, std::size_t count)
{
    const model::Score &score = project.score;
    assert(at + count <= score.measures.size());
    if (count == 0) {
        return nullptr;
    }
    project::Project shell = shellOf(project);
    shiftForDelete(shell, at, count, score.measureStartTick(at), score.measureStartTick(at + count));
    std::vector<model::Measure> survivor = carriedSurvivor(score, at, count);
    const std::size_t removed = count + survivor.size();
    return std::make_unique<ShellEdit>(std::move(shell), at, removed, std::move(survivor), DirtyRange::all(true));
}

std::unique_ptr<Command> setSection(const model::Score &score,
                                    std::size_t measure,
                                    std::string letter,
                                    std::string name,
                                    model::IdGenerator &ids)
{
    assert(measure < score.measures.size());
    if (letter.size() > io::maxNameBytes || name.size() > io::maxNameBytes) {
        return nullptr;
    }
    std::vector<model::Section> sections = score.sections;
    const auto it = std::ranges::find_if(sections, [measure](const model::Section &section) {
        return section.startMeasure >= measure;
    });
    const bool exists = it != sections.end() && it->startMeasure == measure;
    if (letter.empty() && name.empty()) {
        if (!exists) {
            return nullptr;
        }
        sections.erase(it);
    } else if (exists) {
        if (it->letter == letter && it->name == name) {
            return nullptr;
        }
        it->letter = std::move(letter);
        it->name = std::move(name);
    } else {
        sections.insert(it, model::Section{ids.next(), measure, std::move(letter), std::move(name)});
    }
    return std::make_unique<ValueEdit<std::vector<model::Section>>>(
        [](project::Project &p) -> std::vector<model::Section> & {
            return p.score.sections;
        },
        std::move(sections),
        DirtyRange::measures(measure, measure));
}

std::unique_ptr<Command> setTrack(const model::Score &score, std::size_t index, model::Track track)
{
    assert(index < score.tracks.size());
    const model::Track &current = score.tracks[index];
    if (track.name.size() > io::maxNameBytes) {
        return nullptr;
    }
    track.id = current.id; // the id is not editable
    const bool same = current.name == track.name && current.role == track.role
        && current.tuning.strings == track.tuning.strings && current.tuning.capo == track.tuning.capo;
    if (same) {
        return nullptr;
    }
    return std::make_unique<ValueEdit<model::Track>>(
        [index](project::Project &p) -> model::Track & {
            return p.score.tracks[index];
        },
        std::move(track),
        DirtyRange::all());
}

} // namespace cc::edit
