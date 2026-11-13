// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The audio track (components.md section 2.5): time <-> x, measures from the tick map pinned by sync points,
// drag = measure selection, click = seek, double click = sync point request. Screenshot: shots/audio-track.png.
#include "core/model/scorebuilder.h"
#include "ui/copy/audiotrackview.h"
#include "ui/shell/apppaths.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QTest>
#include <QWheelEvent>

#include <cmath>
#include <filesystem>
#include <optional>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                   std::filesystem::path(CC_SOUND_DIR),
                   std::filesystem::temp_directory_path() / "copycoach-tests" / "audiotrack"};
    Theme theme;

    Fixture()
    {
        REQUIRE(theme.load(paths.theme(true)));
    }
};

/// Four 4/4 measures at 120 bpm (2 s each) with a synthetic 8 s waveform.
model::Score fourBars()
{
    model::IdGenerator ids = model::IdGenerator::seeded(41);
    model::ScoreSpec spec;
    spec.tempoBpm = 120.0;
    spec.measures.assign(4, model::MeasureSpec{{{model::Dur::Whole, {{1, 3}}}}});
    return model::buildScore(spec, ids);
}

media::Peaks ramp()
{
    media::Peaks peaks;
    peaks.seconds = 8.0;
    peaks.sampleRate = 48000.0;
    for (int i = 0; i < 800; ++i) {
        peaks.buckets.push_back(static_cast<float>(i) / 800.0F);
    }
    return peaks;
}

} // namespace

TEST_CASE("audiotrack: time maps to x across the file and measures follow the sync points")
{
    const Fixture f;
    AudioTrackView track(f.theme);
    track.resize(800, 120);
    track.setPeaks(ramp());
    REQUIRE(track.xAt(0.0) == 0);
    REQUIRE(track.xAt(4.0) == 400);
    REQUIRE(std::abs(track.secondsAt(600) - 6.0) < 0.01);
    REQUIRE_FALSE(track.measureAt(100)); // no timing yet

    track.setTiming(fourBars(), {}); // score tempo only: 2 s per bar
    REQUIRE(track.measureAt(100) == 0);
    REQUIRE(track.measureAt(300) == 1);
    REQUIRE(track.measureAt(799) == 3);

    // a sync point says bar 3 (tick 7680) starts at 5 s instead of 4 s: bars 1-2 stretch, bar 3 starts later
    track.setTiming(fourBars(), {{0, 0.0}, {7680, 5.0}});
    REQUIRE(track.measureAt(450) == 1); // 4.5 s is still bar 2 now
    REQUIRE(track.measureAt(520) == 2);
}

TEST_CASE("audiotrack: drag selects measures, a click seeks, a double click asks for a sync point")
{
    const Fixture f;
    AudioTrackView track(f.theme);
    track.resize(800, 120);
    track.show();
    track.setPeaks(ramp());
    track.setTiming(fourBars(), {});
    std::optional<std::pair<std::size_t, std::size_t>> selected;
    std::optional<double> seek;
    std::optional<double> sync;
    QObject::connect(&track, &AudioTrackView::measuresSelected, &track, [&selected](std::size_t a, std::size_t b) {
        selected = std::pair{a, b};
    });
    QObject::connect(&track, &AudioTrackView::seekRequested, &track, [&seek](double s) {
        seek = s;
    });
    QObject::connect(&track, &AudioTrackView::syncPointRequested, &track, [&sync](double s) {
        sync = s;
    });

    QTest::mousePress(&track, Qt::LeftButton, Qt::NoModifier, QPoint(100, 60));
    QTest::mouseMove(&track, QPoint(500, 60));
    QTest::mouseRelease(&track, Qt::LeftButton, Qt::NoModifier, QPoint(500, 60));
    REQUIRE(selected == std::pair<std::size_t, std::size_t>{0, 2});
    REQUIRE(track.selectedMeasures() == selected);
    REQUIRE_FALSE(seek);

    QTest::mouseClick(&track, Qt::LeftButton, Qt::NoModifier, QPoint(300, 60));
    REQUIRE(seek);
    REQUIRE(std::abs(seek.value_or(-1.0) - 3.0) < 0.02);
    REQUIRE_FALSE(track.selectedMeasures()); // a plain click clears the selection

    QTest::mouseDClick(&track, Qt::LeftButton, Qt::NoModifier, QPoint(700, 60));
    REQUIRE(sync);
    REQUIRE(std::abs(sync.value_or(-1.0) - 7.0) < 0.02);

    track.setSelectedMeasures(std::pair<std::size_t, std::size_t>{1, 2});
    track.setPlayheadSeconds(2.5);
    track.setTiming(fourBars(), {{0, 0.0}, {7680, 5.0}});
    QTest::qWait(20);
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(track.grab().save(shots.filePath(QStringLiteral("audio-track.png"))));
}

TEST_CASE("audiotrack: zoom keeps the time under the pointer, scrolls with the wheel and follows the playhead")
{
    const Fixture f;
    AudioTrackView track(f.theme);
    track.resize(800, 120);
    track.show();
    track.setPeaks(ramp()); // 8 s across 800 px
    REQUIRE(track.zoom() == 1.0);
    REQUIRE(track.visibleSeconds() == 8.0);

    track.zoomBy(2.0, 400); // the 4 s mark stays under x = 400, so 2..6 s is visible
    REQUIRE(track.zoom() == 2.0);
    REQUIRE(std::abs(track.secondsAt(400) - 4.0) < 0.01);
    REQUIRE(std::abs(track.scrollSeconds() - 2.0) < 0.01);
    REQUIRE(track.xAt(2.0) == 0);
    REQUIRE(track.xAt(6.0) == 800);
    REQUIRE(track.measureAt(0) == std::nullopt); // still no timing

    track.setScrollSeconds(10.0); // clamps to the last visible span: 4..8 s
    REQUIRE(std::abs(track.scrollSeconds() - 4.0) < 0.01);
    track.setScrollSeconds(0.0);

    QWheelEvent wheel(QPointF(100, 60),
                      QPointF(100, 60),
                      QPoint(),
                      QPoint(0, -120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);
    QCoreApplication::sendEvent(&track, &wheel); // one notch down: a tenth of the visible 4 s
    REQUIRE(std::abs(track.scrollSeconds() - 0.4) < 0.01);

    track.zoomBy(2.0, 400); // 4x: 2 s visible, 1.4..3.4 s
    REQUIRE(std::abs(track.scrollSeconds() - 1.4) < 0.01);
    track.setPlayheadSeconds(5.0); // outside the view: it jumps so the playhead sits a tenth of the width in
    REQUIRE(std::abs(track.secondsAt(80) - 5.0) < 0.05);
    track.setPlayheadSeconds(7.9); // near the end: the view stops at the file's end instead
    REQUIRE(std::abs(track.scrollSeconds() - 6.0) < 0.01);

    track.setZoom(0.5); // clamps to fit
    REQUIRE(track.zoom() == 1.0);
    REQUIRE(track.xAt(4.0) == 400);
    REQUIRE(track.scrollSeconds() == 0.0);
}
