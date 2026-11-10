// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Overlays and mouse on the score view (score-data-path.md section 6): a click lands the cursor on the beat,
// shift + click selects measures, a double click asks to seek to the beat's tick, and the playhead sits
// between two beats in proportion to the ticks played. Screenshot: <build>/shots/score-overlays.png.
#include "core/score/musicxmlimport.h"
#include "ui/score/scoreview.h"
#include "ui/shell/apppaths.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QMouseEvent>
#include <QTest>

#include <filesystem>
#include <optional>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                   std::filesystem::path(CC_SOUND_DIR),
                   std::filesystem::temp_directory_path() / "copycoach-tests" / "scoreoverlay"};
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

BeatLocation cursorOf(const ScoreView &view)
{
    const auto cursor = view.beatCursor();
    REQUIRE(cursor.has_value());
    return cursor.value_or(BeatLocation{});
}

QRectF rectOf(ScoreView &view, const std::string &id)
{
    const auto rect = view.rectFor(id);
    REQUIRE(rect.has_value());
    return rect.value_or(QRectF());
}

/// Counts pixels of the colour in a vertical strip, to find the playhead line.
int columnOfColor(const QImage &image, const QColor &color)
{
    for (int x = 0; x < image.width(); ++x) {
        int hits = 0;
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixelColor(x, y) == color) {
                ++hits;
            }
        }
        if (hits > 40) {
            return x;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("scoreview: click, shift-click and double-click drive the cursor, the selection and seeking")
{
    Fixture f;
    ScoreView view(f.theme, std::filesystem::path(CC_VEROVIO_DATA_DIR));
    view.resize(900, 600);
    view.show();
    view.setScore(f.score);
    QTest::qWait(20);
    REQUIRE_FALSE(view.beatCursor());

    const model::Beat &first = f.score.measures[0].beats.front();
    const model::Beat &bridge = f.score.measures[1].beats.front();
    const QPoint firstCentre = rectOf(view, "b-" + first.id).center().toPoint();
    const QPoint bridgeCentre = rectOf(view, "b-" + bridge.id).center().toPoint();

    int cursorSignals = 0;
    std::optional<std::int64_t> seek;
    QObject::connect(&view, &ScoreView::cursorChanged, &view, [&cursorSignals] {
        ++cursorSignals;
    });
    QObject::connect(&view, &ScoreView::seekRequested, &view, [&seek](std::int64_t tick) {
        seek = tick;
    });

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstCentre);
    REQUIRE(view.beatCursor() == BeatLocation{0, first.track, first.voice, first.tick});
    REQUIRE(cursorSignals == 1);
    REQUIRE_FALSE(view.selectedMeasures());

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, bridgeCentre);
    REQUIRE(view.selectedMeasures() == std::pair<std::size_t, std::size_t>{0, 1});
    REQUIRE(cursorOf(view).measure == 0); // shift keeps the anchor

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, bridgeCentre);
    REQUIRE(seek == f.score.measureStartTick(1) + bridge.tick);
    REQUIRE(cursorOf(view).measure == 1);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5)); // empty canvas: no change
    REQUIRE(cursorOf(view).measure == 1);
}

TEST_CASE("scoreview: the playhead slides between two beats and the overlays land in the screenshot")
{
    Fixture f;
    ScoreView view(f.theme, std::filesystem::path(CC_VEROVIO_DATA_DIR));
    view.resize(900, 600);
    view.show();
    view.setScore(f.score);
    QTest::qWait(20);

    const model::Beat &first = f.score.measures[0].beats[0];
    const model::Beat &second = f.score.measures[0].beats[1];
    const QRectF firstRect = rectOf(view, "b-" + first.id);
    const QRectF secondRect = rectOf(view, "b-" + second.id);
    const QColor playhead = f.theme.color(theme::Token::PlayheadColor);

    view.setPlayheadTick(first.tick); // on the first beat
    const int atStart = columnOfColor(view.grab().toImage(), playhead);
    REQUIRE(atStart >= 0);
    REQUIRE(std::abs(atStart - static_cast<int>(firstRect.left())) <= 3);

    view.setPlayheadTick((first.tick + second.tick) / 2); // halfway in ticks: halfway in pixels
    const int halfway = columnOfColor(view.grab().toImage(), playhead);
    const int expected = static_cast<int>((firstRect.left() + secondRect.left()) / 2);
    REQUIRE(std::abs(halfway - expected) <= 3);

    view.setPlayheadTick(std::nullopt);
    REQUIRE(columnOfColor(view.grab().toImage(), playhead) == -1);

    view.setBeatCursor(BeatLocation{0, first.track, first.voice, first.tick});
    view.setSelectedMeasures(std::pair<std::size_t, std::size_t>{1, 1});
    view.setPlayheadTick(second.tick);
    QTest::qWait(20);
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(view.grab().save(shots.filePath(QStringLiteral("score-overlays.png"))));
}

TEST_CASE("scoreview: predicted chords are drawn above their measure and vanish when cleared")
{
    Fixture f;
    ScoreView view(f.theme, std::filesystem::path(CC_VEROVIO_DATA_DIR));
    view.resize(900, 600);
    view.show();
    view.setScore(f.score);
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    const QRectF measure = rectOf(view, "m-" + f.score.measures[0].id);

    model::PredictedItem chord;
    chord.tick = 0;
    chord.text = "Dm7";
    view.setPredictedChords({chord});
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    REQUIRE(after != before);
    int top = after.height();
    int bottom = -1;
    for (int y = 0; y < after.height(); ++y) {
        for (int x = 0; x < after.width(); ++x) {
            if (after.pixel(x, y) != before.pixel(x, y)) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    REQUIRE(bottom >= 0);
    REQUIRE(bottom <= static_cast<int>(measure.top())); // the text sits above the measure box
    REQUIRE(top >= 0);

    view.setPredictedChords({});
    QTest::qWait(20);
    REQUIRE(view.grab().toImage() == before);
}

TEST_CASE("scoreview: predicted notes are drawn inside their measure box and vanish when cleared")
{
    Fixture f;
    ScoreView view(f.theme, std::filesystem::path(CC_VEROVIO_DATA_DIR));
    view.resize(900, 600);
    view.show();
    view.setScore(f.score);
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    const QRectF measure = rectOf(view, "m-" + f.score.measures[0].id);

    model::PredictedItem note;
    note.tick = 0;
    note.dur = 480;
    note.midi = 52;
    model::PredictedItem later = note;
    later.tick = 960;
    later.midi = 59;
    view.setPredictedNotes({note, later});
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    REQUIRE(after != before);
    int left = after.width();
    int right = -1;
    for (int y = 0; y < after.height(); ++y) {
        for (int x = 0; x < after.width(); ++x) {
            if (after.pixel(x, y) != before.pixel(x, y)) {
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
    }
    REQUIRE(right >= 0);
    REQUIRE(left >= static_cast<int>(measure.left()) - 1); // the bars stay inside the first measure's box
    REQUIRE(right <= static_cast<int>(measure.right()) + 1);

    view.setPredictedNotes({});
    QTest::qWait(20);
    REQUIRE(view.grab().toImage() == before);
}
