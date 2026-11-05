// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/beatgrid.h"

#include <algorithm>

namespace cc::ui
{

std::size_t measuresNeeded(const model::Score &score, std::span<const double> downbeats)
{
    return downbeats.size() > score.measures.size() ? downbeats.size() - score.measures.size() : 0;
}

std::vector<model::SyncPoint> syncPointsFromDownbeats(const model::Score &score, std::span<const double> downbeats)
{
    const std::size_t count = std::min(downbeats.size(), score.measures.size());
    std::vector<model::SyncPoint> points;
    points.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        points.push_back(model::SyncPoint{score.measureStartTick(k), downbeats[k]});
    }
    return points;
}

} // namespace cc::ui
