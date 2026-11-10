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
constexpr double chordRisePx = 4.0; // the predicted symbol's baseline above the measure box
constexpr float noteAlpha = 0.55F;
constexpr double noteMinWidthPx = 2.0;
constexpr int noteMarginMidi = 2; // air above and below the predicted range

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

/// Where a predicted chord's text starts: above the measure box, across it in proportion to the tick.
std::optional<QPointF> chordPoint(const OverlayContext &ctx, const model::PredictedItem &item)
{
    const auto measure = model::measureAtTick(*ctx.score, item.tick);
    if (!measure) {
        return std::nullopt;
    }
    const auto rect = measureRect(ctx, *measure);
    if (!rect) {
        return std::nullopt;
    }
    const auto length = static_cast<double>(model::measureTicks(ctx.score->timeSigAt(*measure)));
    const auto offset = static_cast<double>(item.tick - ctx.score->measureStartTick(*measure));
    const double fraction = length > 0.0 ? std::clamp(offset / length, 0.0, 1.0) : 0.0;
    return QPointF(rect->left() + (rect->width() * fraction), rect->top() - chordRisePx);
}

/// Predicted notes (copy-spec.md section 4 "음", C-6): a bar per note inside the measure box it falls in,
/// x by tick and duration, y by pitch over the range of all predicted notes (a measure-aligned piano roll
/// rather than engraved notes: the layer never enters the MEI, notes enter through copy.predict.accept).
class PredictedNotesOverlay final : public IScoreOverlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override
    {
        const std::vector<model::PredictedItem> &notes = ctx.session->predictedNotes;
        if (notes.empty()) {
            return;
        }
        int low = 127;
        int high = 0;
        for (const model::PredictedItem &item : notes) {
            low = std::min(low, static_cast<int>(item.midi.value_or(low)));
            high = std::max(high, static_cast<int>(item.midi.value_or(high)));
        }
        low -= noteMarginMidi;
        high += noteMarginMidi;
        QColor fill = ctx.theme->color(theme::Token::PredictionColor);
        fill.setAlphaF(noteAlpha);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        for (const model::PredictedItem &item : notes) {
            if (const auto rect = noteRect(ctx, item, low, high)) {
                painter.drawRect(*rect);
            }
        }
    }

private:
    static std::optional<QRectF>
    noteRect(const OverlayContext &ctx, const model::PredictedItem &item, int low, int high)
    {
        const auto measure = model::measureAtTick(*ctx.score, item.tick);
        if (!measure || !item.midi) {
            return std::nullopt;
        }
        const auto box = measureRect(ctx, *measure);
        if (!box) {
            return std::nullopt;
        }
        const auto length = static_cast<double>(model::measureTicks(ctx.score->timeSigAt(*measure)));
        const auto offset = static_cast<double>(item.tick - ctx.score->measureStartTick(*measure));
        const double x0 = box->left() + (box->width() * std::clamp(offset / length, 0.0, 1.0));
        const double width = std::max(noteMinWidthPx, box->width() * static_cast<double>(item.dur) / length);
        const double rowHeight = box->height() / static_cast<double>(high - low);
        const double y = box->top() + ((high - static_cast<int>(*item.midi)) * rowHeight);
        return QRectF(x0, y, std::min(width, box->right() - x0), rowHeight);
    }
};

/// Predicted chords (copy-spec.md section 4 "코드", C-6): the symbols in the prediction colour above the
/// measure they fall in; a view of the grey layer only, symbols enter the score through copy.predict.accept.
class PredictedChordsOverlay final : public IScoreOverlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override
    {
        if (ctx.session->predictedChords.empty()) {
            return;
        }
        painter.setPen(ctx.theme->color(theme::Token::PredictionColor));
        painter.setFont(ctx.theme->font(theme::TypeStyle::BodyStrong));
        for (const model::PredictedItem &item : ctx.session->predictedChords) {
            if (const auto at = chordPoint(ctx, item)) {
                painter.drawText(*at, QString::fromStdString(item.text));
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
    overlays.push_back(std::make_unique<PredictedNotesOverlay>());
    overlays.push_back(std::make_unique<PredictedChordsOverlay>());
    overlays.push_back(std::make_unique<CursorOverlay>());
    overlays.push_back(std::make_unique<PlayheadOverlay>());
    return overlays;
}

} // namespace cc::ui
