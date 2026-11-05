// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "ui/score/scoredocument.h"

#include <QAbstractScrollArea>

#include <filesystem>
#include <optional>
#include <string>

namespace cc::ui
{

class Theme;

/// The copy workspace's centre: pages of the engraved score stacked vertically, drawn from ScoreDocument's
/// cached pictures, with the empty state of states.md section 3 when nothing is loaded. Page width follows
/// the viewport, zoom is a painter scale. Overlays (playhead, cursor, selection) come in the next piece and
/// draw after the page through rectFor().
class ScoreView final : public QAbstractScrollArea
{
    Q_OBJECT
public:
    static constexpr double minZoom = 0.5;
    static constexpr double maxZoom = 3.0;

    ScoreView(const Theme &theme, const std::filesystem::path &verovioResources, QWidget *parent = nullptr);

    void setScore(const model::Score &score);
    void clear();
    [[nodiscard]] bool hasScore() const;
    [[nodiscard]] int pageCount() const;

    void setZoom(double factor);
    [[nodiscard]] double zoom() const;

    /// An element's rectangle in viewport pixels (all pages, current scroll and zoom), if drawn.
    [[nodiscard]] std::optional<QRectF> rectFor(const std::string &id);
    /// The element of that class under a viewport point; empty when none.
    [[nodiscard]] std::string elementAt(QPoint viewportPoint, const QString &className);

    [[nodiscard]] ScoreDocument &document();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void relayout(); // page width from the viewport, scroll range from the pages
    void applyTheme();
    [[nodiscard]] double pageTop(int page); // in zoomed document pixels
    void paintEmptyState(QPainter &painter);
    void paintPages(QPainter &painter);

    const Theme *m_theme;
    ScoreDocument m_document;
    double m_zoom = 1.0;
    int m_gap = 0; // between pages, from the theme
};

} // namespace cc::ui
