// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/edit/commands.h"
#include "core/edit/snapshot.h"
#include "core/io/limits.h"
#include "core/model/timemap.h"

#include <algorithm>
#include <map>
#include <span>

// Commands on the project fields outside the score (copy-spec.md section 1).
namespace cc::edit
{

namespace
{

constexpr double maxReferenceCents = 100.0;

std::unique_ptr<Command> textEdit(ValueEdit<std::string>::Field field, const std::string &current, std::string text)
{
    if (text.size() > io::maxTextBytes || text == current) {
        return nullptr;
    }
    return std::make_unique<ValueEdit<std::string>>(std::move(field), std::move(text), DirtyRange::none());
}

bool hasRange(const project::Project &project, const model::Id &rangeId)
{
    return std::ranges::any_of(project.ranges, [&rangeId](const project::Range &range) {
        return range.id == rangeId;
    });
}

bool hasSource(const project::Project &project, const model::Id &sourceId)
{
    return std::ranges::any_of(project.sources, [&sourceId](const project::Source &source) {
        return source.id == sourceId;
    });
}

bool validRange(const project::Project &project, const project::Range &range)
{
    return model::isWellFormedId(range.id) && range.name.size() <= io::maxNameBytes && range.barFrom <= range.barTo
        && range.barTo < project.score.measures.size() && range.speedPct >= project::speedPctMin
        && range.speedPct <= project::speedPctMax && range.pitchSemitones >= -project::pitchSemitonesMax
        && range.pitchSemitones <= project::pitchSemitonesMax;
}

} // namespace

std::unique_ptr<Command> replaceScore(const project::Project &project, model::Score score)
{
    if (score.tracks.empty() || score.tracks.size() > io::maxTracks || score.measures.size() > io::maxMeasures) {
        return nullptr;
    }
    static_cast<void>(project);
    return std::make_unique<ValueEdit<model::Score>>(
        [](project::Project &p) -> model::Score & {
            return p.score;
        },
        std::move(score),
        DirtyRange::all(true));
}

std::unique_ptr<Command> setProjectMemo(const project::Project &project, std::string text)
{
    return textEdit(
        [](project::Project &p) -> std::string & {
            return p.memos.project;
        },
        project.memos.project,
        std::move(text));
}

std::unique_ptr<Command> setRangeMemo(const project::Project &project, const model::Id &rangeId, std::string text)
{
    if (text.size() > io::maxTextBytes || !hasRange(project, rangeId)) {
        return nullptr;
    }
    std::map<model::Id, std::string> memos = project.memos.ranges;
    if (text.empty()) {
        if (memos.erase(rangeId) == 0) {
            return nullptr;
        }
    } else {
        const auto it = memos.find(rangeId);
        if (it != memos.end() && it->second == text) {
            return nullptr;
        }
        memos[rangeId] = std::move(text);
    }
    return std::make_unique<ValueEdit<std::map<model::Id, std::string>>>(
        [](project::Project &p) -> std::map<model::Id, std::string> & {
            return p.memos.ranges;
        },
        std::move(memos),
        DirtyRange::none());
}

std::unique_ptr<Command> setLyrics(const project::Project &project, std::string text)
{
    return textEdit(
        [](project::Project &p) -> std::string & {
            return p.lyrics;
        },
        project.lyrics,
        std::move(text));
}

std::unique_ptr<Command> setRange(const project::Project &project, project::Range range)
{
    if (!validRange(project, range)) {
        return nullptr;
    }
    std::vector<project::Range> ranges = project.ranges;
    const auto it = std::ranges::find(ranges, range.id, &project::Range::id);
    if (it != ranges.end()) {
        *it = std::move(range);
    } else {
        ranges.push_back(std::move(range));
    }
    return std::make_unique<ValueEdit<std::vector<project::Range>>>(
        [](project::Project &p) -> std::vector<project::Range> & {
            return p.ranges;
        },
        std::move(ranges),
        DirtyRange::none());
}

std::unique_ptr<Command> removeRange(const project::Project &project, const model::Id &rangeId)
{
    if (!hasRange(project, rangeId)) {
        return nullptr;
    }
    project::Project shell = project;
    shell.score.measures.clear();
    std::erase_if(shell.ranges, [&rangeId](const project::Range &range) {
        return range.id == rangeId;
    });
    shell.memos.ranges.erase(rangeId);
    for (project::Phrase &phrase : shell.phrases) {
        if (phrase.rangeId == rangeId) {
            phrase.rangeId.reset();
        }
    }
    if (shell.lastSession.rangeId == rangeId) {
        shell.lastSession.rangeId.reset();
    }
    return std::make_unique<ShellEdit>(std::move(shell), 0, 0, std::vector<model::Measure>{}, DirtyRange::none());
}

std::unique_ptr<Command>
setSyncPoints(const project::Project &project, const model::Id &sourceId, std::vector<model::SyncPoint> points)
{
    if (!hasSource(project, sourceId) || points.size() > io::maxListItems
        || model::firstInvalidSyncPoint(std::span<const model::SyncPoint>(points))) {
        return nullptr;
    }
    std::vector<project::SourceSyncPoint> all;
    for (const project::SourceSyncPoint &point : project.syncPoints) {
        if (point.sourceId != sourceId) {
            all.push_back(point);
        }
    }
    for (const model::SyncPoint &point : points) {
        all.push_back(project::SourceSyncPoint{sourceId, point});
    }
    const auto same = [](const project::SourceSyncPoint &a, const project::SourceSyncPoint &b) {
        return a.sourceId == b.sourceId && a.point.tick == b.point.tick && a.point.seconds == b.point.seconds;
    };
    if (std::ranges::equal(all, project.syncPoints, same)) {
        return nullptr;
    }
    return std::make_unique<ValueEdit<std::vector<project::SourceSyncPoint>>>(
        [](project::Project &p) -> std::vector<project::SourceSyncPoint> & {
            return p.syncPoints;
        },
        std::move(all),
        DirtyRange::timingOnly());
}

std::unique_ptr<Command> addSource(const project::Project &project, project::Source source)
{
    const bool known = std::ranges::any_of(project.sources, [&source](const project::Source &each) {
        return each.id == source.id;
    });
    if (known || source.path.empty() || source.path.size() > io::maxNameBytes
        || project.sources.size() >= io::maxListItems) {
        return nullptr;
    }
    std::vector<project::Source> sources = project.sources;
    sources.push_back(std::move(source));
    return std::make_unique<ValueEdit<std::vector<project::Source>>>(
        [](project::Project &p) -> std::vector<project::Source> & {
            return p.sources;
        },
        std::move(sources),
        DirtyRange::none());
}

std::unique_ptr<Command> setReference(const project::Project &project, project::Reference reference)
{
    if (reference.pitchCents < -maxReferenceCents || reference.pitchCents > maxReferenceCents
        || reference.tuningNote < 0 || reference.tuningNote >= theory::semitonesPerOctave) {
        return nullptr;
    }
    if (reference.pitchCents == project.reference.pitchCents && reference.tuningNote == project.reference.tuningNote) {
        return nullptr;
    }
    return std::make_unique<ValueEdit<project::Reference>>(
        [](project::Project &p) -> project::Reference & {
            return p.reference;
        },
        reference,
        DirtyRange::none());
}

} // namespace cc::edit
