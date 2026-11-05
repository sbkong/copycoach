// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "core/score/meiwriter.h"
#include "ui/score/scoregeometry.h"

#include <QColor>
#include <QPicture>
#include <QSizeF>
#include <QString>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vrv
{
class Toolkit;
}

namespace cc::ui
{

/// Owns the Verovio toolkit for one score (score-data-path.md section 5): the model goes in as MEI, pages come
/// out as recorded paint commands plus their geometry, rendered lazily and cached until the score, the page
/// width or the font scale changes. Zoom is the caller's painter scale, not a relayout. Main thread only.
class ScoreDocument
{
public:
    struct Page {
        QPicture picture; // paint at (0, 0) in page pixels
        QSizeF sizePx;
        ScoreGeometry geometry;
    };

    static constexpr int defaultFontScale = 40; // Verovio "scale" percent, spike (e)
    static constexpr int minPageWidthPx = 200;

    explicit ScoreDocument(const std::filesystem::path &resourceDir);
    ~ScoreDocument();
    ScoreDocument(const ScoreDocument &) = delete;
    ScoreDocument &operator=(const ScoreDocument &) = delete;
    ScoreDocument(ScoreDocument &&) = delete;
    ScoreDocument &operator=(ScoreDocument &&) = delete;

    [[nodiscard]] bool ready() const; // Verovio found its resources

    void setInk(const QColor &color, const QString &serifFamily);
    /// Page width in pixels at font scale 100 % of the painter; re-lays out when it changes.
    void setPageWidth(int px);
    void setFontScale(int percent);

    bool setScore(const model::Score &score, const score::MeiOptions &options = {});
    void clear();
    [[nodiscard]] bool hasScore() const;
    [[nodiscard]] const std::string &mei() const;

    [[nodiscard]] int pageCount() const;
    const Page &page(int index); // 0-based; renders on first use

    [[nodiscard]] int unsupportedArcCount() const;
    /// Draws a page (1-based) through the given context without caching: printing, PDF export, tests.
    void renderDirect(int pageNumber, QtDeviceContext &dc);

private:
    void applyOptions();
    void reload();
    [[nodiscard]] std::string optionsJson() const;

    std::unique_ptr<vrv::Toolkit> m_toolkit;
    bool m_ready = false;
    std::string m_mei;
    QColor m_ink{Qt::black};
    QString m_serifFamily;
    int m_pageWidthPx = 800;
    int m_fontScale = defaultFontScale;
    std::vector<std::unique_ptr<Page>> m_pages; // null until rendered
    int m_unsupportedArcs = 0;
};

} // namespace cc::ui
