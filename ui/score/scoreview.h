// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "ui/score/overlays.h"
#include "ui/score/scoredocument.h"

#include <QAbstractScrollArea>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cc::ui
{

class Theme;

/// The copy workspace's centre: pages of the engraved score stacked vertically, drawn from ScoreDocument's
/// cached pictures, overlays (selection, cursor, playhead) on top, the empty state of states.md section 3
/// when nothing is loaded. Page width follows the viewport, zoom is a painter scale. Click = cursor on the
/// beat, shift + click = extend the measure selection, double click = seek there.
class ScoreView final : public QAbstractScrollArea
{
    Q_OBJECT
public:
    static constexpr double minZoom = 0.5;
    static constexpr double maxZoom = 3.0;

    ScoreView(const Theme &theme, const std::filesystem::path &verovioResources, QWidget *parent = nullptr);
    ~ScoreView() override;
    Q_DISABLE_COPY_MOVE(ScoreView)

    void setScore(const model::Score &score);
    /// A new version of the same score (an edit): re-engraves, keeps cursor, selection and playhead.
    void updateScore(const model::Score &score);
    void clear();
    [[nodiscard]] bool hasScore() const;
    [[nodiscard]] const model::Score &score() const;
    [[nodiscard]] int pageCount() const;

    void setZoom(double factor);
    [[nodiscard]] double zoom() const;

    // session state the overlays draw
    void setBeatCursor(std::optional<BeatLocation> cursor);
    [[nodiscard]] std::optional<BeatLocation> beatCursor() const;
    void setCursorString(int string);
    void setSelectedMeasures(std::optional<std::pair<std::size_t, std::size_t>> range);
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> selectedMeasures() const;
    /// Absolute tick while playing, nullopt hides the playhead.
    void setPlayheadTick(std::optional<model::Tick> tick);
    [[nodiscard]] const ScoreSession &session() const;

    /// An element's rectangle in viewport pixels (all pages, current scroll and zoom), if drawn.
    [[nodiscard]] std::optional<QRectF> rectFor(const std::string &id);
    /// The element of that class under a viewport point; empty when none.
    [[nodiscard]] std::string elementAt(QPoint viewportPoint, const QString &className);
    /// The beat whose TAB group lies under the point, if any.
    [[nodiscard]] std::optional<BeatLocation> beatAt(QPoint viewportPoint);

    [[nodiscard]] ScoreDocument &document();

signals:
    void cursorChanged();
    void focusChanged(bool focused); // the editor context follows it
    void seekRequested(std::int64_t absoluteTick);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void relayout(); // page width from the viewport, scroll range from the pages
    void applyTheme();
    void indexBeats();
    [[nodiscard]] double pageTop(int page); // in zoomed document pixels
    void paintEmptyState(QPainter &painter);
    void paintPages(QPainter &painter);
    void paintOverlays(QPainter &painter);

    const Theme *m_theme;
    ScoreDocument m_document;
    model::Score m_score;
    std::unordered_map<std::string, BeatLocation> m_beatsById; // "b-<id>" -> where it is
    ScoreSession m_session;
    std::vector<std::unique_ptr<IScoreOverlay>> m_overlays;
    double m_zoom = 1.0;
    int m_gap = 0; // between pages, from the theme
};

} // namespace cc::ui
