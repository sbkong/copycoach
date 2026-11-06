// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The video panel (video.md sections 2 to 5): frames follow the time it is given, decoded off the GUI thread;
// mirror flips the drawing; frame steps ask the transport to follow. Screenshot: shots/video-panel.png.
#include "ui/shell/apppaths.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/video/videopanel.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

const std::filesystem::path s_fixture = std::filesystem::path(CC_FIXTURE_DIR) / "video" / "gray-steps.mp4";

struct Fixture {
    AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                   std::filesystem::path(CC_SOUND_DIR),
                   std::filesystem::temp_directory_path() / "copycoach-tests" / "videopanel"};
    Theme theme;

    Fixture()
    {
        REQUIRE(theme.load(paths.theme(true)));
    }
};

double greyOf(const QImage &image)
{
    double sum = 0.0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            sum += qRed(image.pixel(x, y));
        }
    }
    return sum / (static_cast<double>(image.width()) * image.height());
}

bool shown(const VideoPanel &panel, double seconds)
{
    return QTest::qWaitFor(
        [&] {
            return panel.shownSeconds() && std::abs(*panel.shownSeconds() - seconds) < 0.01;
        },
        5000);
}

} // namespace

TEST_CASE("videopanel: shows the frame for the time it is given and steps a frame at a time")
{
    const Fixture f;
    const Icons icons(f.theme);
    VideoPanel panel(f.theme, icons);
    panel.resize(320, 240);
    panel.show();
    REQUIRE_FALSE(panel.hasVideo());

    panel.setVideo(s_fixture, 0);
    panel.showAt(0.55);
    REQUIRE(shown(panel, 0.5));
    CHECK(panel.fps() == 10.0);
    CHECK(panel.currentFrame().width() == 64);
    CHECK(std::abs(greyOf(panel.currentFrame()) - 66.0) < 10.0);

    QSignalSpy seeks(&panel, &VideoPanel::seekRequested);
    panel.stepFrame(1);
    REQUIRE(seeks.count() == 1);
    CHECK(std::abs(seeks[0][0].toDouble() - 0.65) < 0.001);
    REQUIRE(shown(panel, 0.6));
    CHECK(std::abs(greyOf(panel.currentFrame()) - 78.0) < 10.0);
    panel.stepFrame(-1);
    REQUIRE(shown(panel, 0.5));

    CHECK_FALSE(panel.mirrored());
    panel.setMirrored(true);
    CHECK(panel.mirrored());
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    REQUIRE(panel.grab().save(shots.filePath(QStringLiteral("video-panel.png"))));

    panel.clearVideo();
    CHECK_FALSE(panel.hasVideo());
    CHECK(panel.currentFrame().isNull());
}

TEST_CASE("videopanel: a file that is not video reports through failed()")
{
    const Fixture f;
    const Icons icons(f.theme);
    VideoPanel panel(f.theme, icons);
    QSignalSpy failures(&panel, &VideoPanel::failed);
    panel.setVideo(std::filesystem::path(CC_FIXTURE_DIR) / "video" / "missing.mp4", 0);
    REQUIRE(failures.wait(5000));
}
