// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Proves the generated headers compile and carry the registry/theme content (docs/ui/naming.md section 6).
#include "generated/actionids.h"
#include "generated/panelids.h"
#include "generated/settingkeys.h"
#include "generated/tokens.h"
#include "generated/workspaceids.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace std::string_view_literals;

TEST_CASE("generated: action table matches the enum and keeps default keys")
{
    STATIC_REQUIRE(cc::action::infos.size() == static_cast<std::size_t>(cc::action::Id::Count));
    const auto &playPause = cc::action::info(cc::action::Id::TransportPlayPause);
    REQUIRE(playPause.id == "transport.play-pause"sv);
    REQUIRE(playPause.context == "global"sv);
    REQUIRE(playPause.keyCount >= 1);
    const auto &chordSelect = cc::action::info(cc::action::Id::RoutineChordSelectN);
    REQUIRE(chordSelect.rangeMin == 1);
    REQUIRE(chordSelect.rangeMax == 8);
}

TEST_CASE("generated: every action id is lowercase dotted (naming rule 1)")
{
    for (const auto &info : cc::action::infos) {
        for (const char c : info.id) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '{' || c == '}' || c == 'n';
            REQUIRE(ok);
        }
    }
}

TEST_CASE("generated: panels name their workspaces")
{
    STATIC_REQUIRE(cc::panel::infos.size() == static_cast<std::size_t>(cc::panel::Id::Count));
    const auto &navigator = cc::panel::info(cc::panel::Id::DockNavigator);
    REQUIRE(navigator.workspaceMask != 0u);
    REQUIRE(cc::workspace::idOf(cc::workspace::Id::Routine) == "routine"sv);
    REQUIRE(cc::workspace::ids.size() == static_cast<std::size_t>(cc::workspace::Id::Count));
}

TEST_CASE("generated: settings carry type, default and range")
{
    STATIC_REQUIRE(cc::setting::infos.size() == static_cast<std::size_t>(cc::setting::Key::Count));
    const auto &tempo = cc::setting::info(cc::setting::Key::PracticeTempoDefault);
    REQUIRE(tempo.type == cc::setting::Type::Int);
    REQUIRE(tempo.defaultJson == "60"sv);
    REQUIRE(tempo.rangeMin == 40);
    REQUIRE(tempo.rangeMax == 220);
    const auto &theme = cc::setting::info(cc::setting::Key::AppearanceTheme);
    REQUIRE(theme.type == cc::setting::Type::Enum);
    REQUIRE(theme.valuesJson.find("dark") != std::string_view::npos);
}

TEST_CASE("generated: theme tokens expose names for painters")
{
    STATIC_REQUIRE(cc::theme::tokenNames.size() == static_cast<std::size_t>(cc::theme::Token::Count));
    REQUIRE(cc::theme::name(cc::theme::Token::BackgroundCanvasColor) == "background-canvas-color"sv);
    REQUIRE(cc::theme::name(cc::theme::Token::PlayheadColor) == "playhead-color"sv);
    REQUIRE(cc::theme::name(cc::theme::Spacing::TargetMin) == "target-min"sv);
    REQUIRE(cc::theme::name(cc::theme::TypeStyle::Body) == "body"sv);
}
