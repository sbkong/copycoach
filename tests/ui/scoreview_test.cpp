// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Score rendering (score-data-path.md section 5): the imported MusicXML goes through MEI into Verovio and onto
// a QPainter through QtDeviceContext; the page has ink, the geometry knows our ids, a hit test finds a note
// from its centre, the glyph fonts need no arcs, and the view scrolls pages at a zoom. A screenshot lands in
// <build>/shots/score-import-features.png for a human look.
#include "core/score/musicxmlimport.h"
#include "ui/score/qtdevicecontext.h"
#include "ui/score/scoreview.h"
#include "ui/shell/apppaths.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTest>

#include <filesystem>
#include <string>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                   std::filesystem::path(CC_SOUND_DIR),
                   std::filesystem::temp_directory_path() / "copycoach-tests" / "scoreview"};
    Theme theme;
    model::IdGenerator ids = model::IdGenerator::seeded(1);
    model::Score score;

    Fixture()
    {
        REQUIRE(theme.load(paths.theme(true)));
        Result<score::ScoreImport> r =
            score::importMusicXml(std::filesystem::path(CC_FIXTURE_DIR) / "score" / "import-features.musicxml", ids);
        REQUIRE(r);
        score = std::move(r->score);
    }
};

/// The element's rectangle, failing the test when the page does not know the id.
QRectF rectOf(const ScoreGeometry &geometry, const std::string &id)
{
    const auto rect = geometry.rectFor(id);
    REQUIRE(rect.has_value());
    return rect.value_or(QRectF());
}

QRectF rectOf(ScoreView &view, const std::string &id)
{
    const auto rect = view.rectFor(id);
    REQUIRE(rect.has_value());
    return rect.value_or(QRectF());
}

int inkPixels(const QImage &image, const QColor &background)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != background) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

TEST_CASE("scoreview: a page renders with ink, carries our ids and answers a hit test at a note's centre")
{
    Fixture f;
    ScoreDocument document(std::filesystem::path(CC_VEROVIO_DATA_DIR));
    REQUIRE(document.ready());
    document.setPageWidth(900);
    REQUIRE(document.setScore(f.score));
    REQUIRE(document.pageCount() >= 1);
    const ScoreDocument::Page &page = document.page(0);
    REQUIRE(page.sizePx.width() > 0);
    REQUIRE(page.sizePx.height() > 0);
    REQUIRE(document.unsupportedArcCount() == 0);

    QImage image(page.sizePx.toSize(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.drawPicture(0, 0, page.picture);
    }
    REQUIRE(inkPixels(image, Qt::white) > 1000);

    const model::Beat &first = f.score.measures[0].beats.front();
    const QRectF tab = rectOf(page.geometry, "b-" + first.id);
    const QRectF staffNote = rectOf(page.geometry, "ns-" + first.notes[0].id);
    const QRectF measure = rectOf(page.geometry, "m-" + f.score.measures[0].id);
    REQUIRE(measure.contains(tab));
    REQUIRE(staffNote.top() < tab.top()); // the notation staff sits above its TAB
    REQUIRE(page.geometry.elementAt(staffNote.center(), QStringLiteral("note")) == "ns-" + first.notes[0].id);
    REQUIRE(page.geometry.elementAt(tab.center(), QStringLiteral("tabGrp")) == "b-" + first.id);
}

TEST_CASE("scoreview: the widget shows the empty state, then pages that follow the viewport width and zoom")
{
    Fixture f;
    ScoreView view(f.theme, std::filesystem::path(CC_VEROVIO_DATA_DIR));
    view.resize(900, 600);
    view.show();
    QTest::qWait(20);
    REQUIRE_FALSE(view.hasScore());
    REQUIRE(view.pageCount() == 0);
    const QImage empty = view.grab().toImage();
    REQUIRE(inkPixels(empty, f.theme.color(theme::Token::BackgroundCanvasColor)) > 0); // the empty-state text

    view.setScore(f.score);
    QTest::qWait(20);
    REQUIRE(view.hasScore());
    REQUIRE(view.pageCount() >= 1);
    const model::Beat &first = f.score.measures[0].beats.front();
    const QRectF rect = rectOf(view, "b-" + first.id);
    REQUIRE(rect.width() > 0);
    REQUIRE(view.elementAt(rect.center().toPoint(), QStringLiteral("tabGrp")) == "b-" + first.id);

    view.setZoom(2.0);
    QTest::qWait(20);
    const QRectF zoomed = rectOf(view, "b-" + first.id);
    REQUIRE(zoomed.width() > rect.width());

    view.setZoom(1.0);
    QTest::qWait(20);
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(view.grab().save(shots.filePath(QStringLiteral("score-import-features.png"))));

    view.clear();
    REQUIRE_FALSE(view.hasScore());
}

TEST_CASE("scoreview: replaying the recorded page equals drawing the page straight onto an image")
{
    const Fixture f;
    ScoreDocument document(std::filesystem::path(CC_VEROVIO_DATA_DIR));
    document.setPageWidth(900);
    REQUIRE(document.setScore(f.score));
    const ScoreDocument::Page &page = document.page(0);
    QImage replayed(page.sizePx.toSize(), QImage::Format_ARGB32);
    replayed.fill(Qt::white);
    {
        QPainter painter(&replayed);
        painter.drawPicture(0, 0, page.picture);
    }
    QImage direct(page.sizePx.toSize(), QImage::Format_ARGB32);
    direct.fill(Qt::white);
    {
        QPainter painter(&direct);
        QtDeviceContext dc(&painter);
        document.renderDirect(1, dc);
    }
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(replayed.save(shots.filePath(QStringLiteral("score-page-replayed.png"))));
    REQUIRE(direct.save(shots.filePath(QStringLiteral("score-page-direct.png"))));
    REQUIRE(inkPixels(direct, Qt::white) > 1000);
    int different = 0;
    for (int y = 0; y < direct.height(); ++y) {
        for (int x = 0; x < direct.width(); ++x) {
            if (direct.pixelColor(x, y) != replayed.pixelColor(x, y)) {
                ++different;
            }
        }
    }
    INFO("pixels that differ: " << different);
    REQUIRE(different < direct.width() * direct.height() / 100);
}
