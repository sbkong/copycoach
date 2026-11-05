// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Predicted downbeats -> sync points (copy-spec.md section 4): one per bar, extra downbeats need extra bars.
#include "core/model/scorebuilder.h"
#include "ui/copy/beatgrid.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace cc;
using namespace cc::ui;

namespace
{

model::Score bars(std::size_t count)
{
    model::IdGenerator ids = model::IdGenerator::seeded(7);
    model::ScoreSpec spec;
    spec.measures.assign(count, model::MeasureSpec{{{model::Dur::Whole, {{1, 3}}}}});
    return model::buildScore(spec, ids);
}

} // namespace

TEST_CASE("beatgrid: downbeat k pins the start of measure k, the rest ask for more bars")
{
    const model::Score score = bars(2);
    const std::array<double, 4> downbeats{0.5, 2.5, 4.5, 6.5};
    REQUIRE(measuresNeeded(score, downbeats) == 2);
    const std::vector<model::SyncPoint> points = syncPointsFromDownbeats(score, downbeats);
    REQUIRE(points.size() == 2);
    CHECK(points[0].tick == 0);
    CHECK(points[0].seconds == 0.5);
    CHECK(points[1].tick == score.measureStartTick(1));
    CHECK(points[1].seconds == 2.5);
    CHECK(measuresNeeded(bars(8), downbeats) == 0);
}
