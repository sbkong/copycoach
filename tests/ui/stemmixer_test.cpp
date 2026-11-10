// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// The stem mixer dock (components.md "스템 믹서"): before separation one button that asks for it, after it a
// row per stem whose mute and solo buttons report their changes.
#include "ui/copy/stemmixer.h"
#include "ui/shell/apppaths.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QSignalSpy>

#include <filesystem>

using namespace cc;
using namespace cc::ui;

TEST_CASE("stemmixer: the button before separation, rows with mute and solo after")
{
    const AppPaths paths{std::filesystem::path(CC_DATA_DIR),
                         std::filesystem::path(CC_SOUND_DIR),
                         std::filesystem::temp_directory_path() / "copycoach-tests" / "stemmixer"};
    Theme theme;
    REQUIRE(theme.load(paths.theme(true)));
    StemMixerPanel mixer(theme);
    mixer.show();
    REQUIRE_FALSE(mixer.hasStems());
    const QSignalSpy asked(&mixer, &StemMixerPanel::separateRequested);
    mixer.separateButton()->click();
    REQUIRE(asked.count() == 1);

    mixer.setStems(
        {QStringLiteral("보컬"), QStringLiteral("드럼"), QStringLiteral("베이스"), QStringLiteral("나머지")});
    REQUIRE(mixer.stemCount() == 4);
    REQUIRE_FALSE(mixer.separateButton()->isVisibleTo(&mixer));
    const QSignalSpy muted(&mixer, &StemMixerPanel::muteChanged);
    const QSignalSpy soloed(&mixer, &StemMixerPanel::soloChanged);
    mixer.toggleMute(1);
    REQUIRE(mixer.isMuted(1));
    REQUIRE(muted.count() == 1);
    REQUIRE(muted.at(0).at(0).toULongLong() == 1);
    REQUIRE(muted.at(0).at(1).toBool());
    mixer.setSolo(3, true);
    REQUIRE(mixer.isSolo(3));
    REQUIRE(soloed.count() == 1);
    mixer.toggleMute(9); // out of range: nothing happens
    REQUIRE(muted.count() == 1);

    mixer.clear();
    REQUIRE_FALSE(mixer.hasStems());
    REQUIRE(mixer.separateButton()->isVisibleTo(&mixer));
}
