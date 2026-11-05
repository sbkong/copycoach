// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"

#include <QPainter>
#include <QRectF>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cc::ui
{

class Theme;

/// A beat position as the session names it (not a model type: the cursor is session state, section 2).
struct BeatLocation {
    std::size_t measure = 0;
    std::size_t track = 0;
    int voice = 1;
    model::Tick tick = 0; // inside the measure
    constexpr bool operator==(const BeatLocation &) const = default;
};

/// What the overlays draw from (score-data-path.md section 6): cursor, selection and playhead.
struct ScoreSession {
    std::optional<BeatLocation> cursor;
    std::optional<std::pair<std::size_t, std::size_t>> selectedMeasures; // inclusive
    std::optional<model::Tick> playheadTick; // absolute, while playing
    std::size_t playheadTrack = 0;
};

/// Everything an overlay may use. rectFor answers in the painter's coordinates, nullopt when the element is
/// not on a rendered page; overlays never see the renderer.
struct OverlayContext {
    const model::Score *score = nullptr;
    const ScoreSession *session = nullptr;
    const Theme *theme = nullptr;
    std::function<std::optional<QRectF>(const std::string &id)> rectFor;
};

class IScoreOverlay
{
public:
    virtual ~IScoreOverlay() = default;
    IScoreOverlay(const IScoreOverlay &) = delete;
    IScoreOverlay &operator=(const IScoreOverlay &) = delete;
    IScoreOverlay(IScoreOverlay &&) = delete;
    IScoreOverlay &operator=(IScoreOverlay &&) = delete;

    virtual void paint(QPainter &painter, const OverlayContext &context) = 0;

protected:
    IScoreOverlay() = default;
};

/// The fixed list, in paint order: selection under everything, then the cursor, the playhead on top.
std::vector<std::unique_ptr<IScoreOverlay>> standardOverlays();

} // namespace cc::ui
