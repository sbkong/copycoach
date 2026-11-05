// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/score/overlays.h"

#include "core/model/locate.h"
#include "ui/theme/theme.h"

#include <QPen>

#include <algorithm>

namespace cc::ui
{

namespace
{

constexpr int playheadWidthPx = 3; // score-data-path.md section 6
constexpr int cursorWidthPx = 2;
constexpr double selectionAlpha = 0.25;

/// The measure's box, which spans every staff of its system: the vertical extent for lines.
std::optional<QRectF> measureRect(const OverlayContext &ctx, std::size_t measure)
{
    return ctx.rectFor("m-" + ctx.score->measures[measure].id);
}

const model::Beat *beatAt(const model::Score &score, const BeatLocation &at)
{
    if (at.measure >= score.measures.size()) {
        return nullptr;
    }
    for (const model::Beat &beat : score.measures[at.measure].beats) {
        if (beat.track == at.track && beat.voice == at.voice && beat.tick == at.tick) {
            return &beat;
        }
    }
    return nullptr;
}

/// Selection: the measures' boxes, filled with the selection color.
class SelectionOverlay final : public IScoreOverlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override
    {
        if (!ctx.session->selectedMeasures) {
            return;
        }
        QColor fill = ctx.theme->color(theme::Token::SelectionColor);
        fill.setAlphaF(selectionAlpha);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        const auto [from, to] = *ctx.session->selectedMeasures;
        for (std::size_t m = from; m <= to && m < ctx.score->measures.size(); ++m) {
            if (const auto rect = measureRect(ctx, m)) {
                painter.drawRect(*rect);
            }
        }
    }
};

/// An empty slot (no beat there yet) is shown at the measure's right edge, as wide as a tab digit.
std::optional<QRectF> emptySlotRect(const OverlayContext &ctx, const BeatLocation &at)
{
    constexpr double slotWidth = 12.0;
    constexpr double slotMargin = 6.0;
    const auto measure = measureRect(ctx, at.measure);
    if (!measure) {
        return std::nullopt;
    }
    return QRectF(measure->right() - slotMargin - slotWidth, measure->top(), slotWidth, measure->height());
}

/// Cursor: the beat's TAB group outlined in the voice color.
class CursorOverlay final : public IScoreOverlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override
    {
        if (!ctx.session->cursor) {
            return;
        }
        const model::Beat *beat = beatAt(*ctx.score, *ctx.session->cursor);
        const auto rect = beat != nullptr ? ctx.rectFor("b-" + beat->id) : emptySlotRect(ctx, *ctx.session->cursor);
        if (!rect) {
            return;
        }
        const theme::Token token = ctx.session->cursor->voice == 2 ? theme::Token::ScoreCursorVoice2Color
                                                                   : theme::Token::ScoreCursorVoice1Color;
        painter.setPen(QPen(ctx.theme->color(token), cursorWidthPx));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect->adjusted(-cursorWidthPx, -cursorWidthPx, cursorWidthPx, cursorWidthPx));
    }
};

/// Playhead: a vertical line through the system, sliding from the current beat toward the next one in
/// proportion to the ticks elapsed, when both sit on the same system.
class PlayheadOverlay final : public IScoreOverlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override
    {
        const std::optional<model::Tick> playhead = ctx.session->playheadTick;
        if (!playhead) {
            return;
        }
        const auto located = model::locateBeat(*ctx.score, *playhead, ctx.session->playheadTrack);
        if (!located) {
            return;
        }
        const auto beatRect = ctx.rectFor("b-" + located->beat->id);
        const auto system = measureRect(ctx, located->measure);
        if (!beatRect || !system) {
            return;
        }
        painter.setPen(QPen(ctx.theme->color(theme::Token::PlayheadColor), playheadWidthPx));
        const double x = xBetween(ctx, *located, *beatRect, system->top(), *playhead);
        painter.drawLine(QPointF(x, system->top()), QPointF(x, system->bottom()));
    }

private:
    /// Left edge of the beat, moved toward the next beat's left edge by the fraction of the gap played.
    static double xBetween(const OverlayContext &ctx,
                           const model::LocatedBeat &located,
                           const QRectF &beatRect,
                           double systemTop,
                           model::Tick playhead)
    {
        const auto span = static_cast<double>(located.nextTick - located.startTick);
        if (span <= 0.0) {
            return beatRect.left();
        }
        const double fraction = std::clamp(static_cast<double>(playhead - located.startTick) / span, 0.0, 1.0);
        const auto next = model::locateBeat(*ctx.score, located.nextTick, ctx.session->playheadTrack);
        if (!next || next->startTick != located.nextTick) {
            return beatRect.left();
        }
        const auto nextRect = ctx.rectFor("b-" + next->beat->id);
        const auto nextSystem = measureRect(ctx, next->measure);
        if (!nextRect || !nextSystem || nextSystem->top() != systemTop) {
            return beatRect.left();
        }
        return beatRect.left() + ((nextRect->left() - beatRect.left()) * fraction);
    }
};

} // namespace

std::vector<std::unique_ptr<IScoreOverlay>> standardOverlays()
{
    std::vector<std::unique_ptr<IScoreOverlay>> overlays;
    overlays.push_back(std::make_unique<SelectionOverlay>());
    overlays.push_back(std::make_unique<CursorOverlay>());
    overlays.push_back(std::make_unique<PlayheadOverlay>());
    return overlays;
}

} // namespace cc::ui
