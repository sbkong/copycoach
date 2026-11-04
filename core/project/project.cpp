// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/project/project.h"

#include "core/project/uuid.h"

namespace cc::project
{

Project newProject()
{
    Project project;
    project.projectId = newUuidV4();
    model::IdGenerator ids;
    project.score.tracks.push_back(
        model::Track{ids.next(), "Guitar", theory::Tuning::standardSix(), model::TrackRole::Melody});
    return project;
}

std::vector<model::SyncPoint> syncPointsFor(const Project &project, const model::Id &sourceId)
{
    std::vector<model::SyncPoint> points;
    for (const SourceSyncPoint &sync : project.syncPoints) {
        if (sync.sourceId == sourceId) {
            points.push_back(sync.point);
        }
    }
    return points;
}

} // namespace cc::project
