// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Theme loader against the real token files (qpalette.md sections 2-3, naming.md section 2).
#include "ui/theme/icons.h"
#include "ui/theme/style.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QFont>
#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

std::filesystem::path themePath(const char *name)
{
    return std::filesystem::path(CC_DATA_DIR) / "ui" / name;
}

} // namespace

TEST_CASE("theme: both token files load, every colour token resolves except the computed one")
{
    for (const char *file : {"theme-dark.json", "theme-light.json"}) {
        Theme theme;
        const auto loaded = theme.load(themePath(file));
        INFO(file);
        REQUIRE(loaded);
        for (std::size_t i = 0; i < theme::tokenNames.size(); ++i) {
            const auto token = static_cast<theme::Token>(i);
            INFO(theme::tokenNames[i]);
            REQUIRE((theme.color(token).isValid() || token == theme::Token::FretboardDotLabelColor));
        }
        REQUIRE(theme.spacing(theme::Spacing::Unit) == 4);
        REQUIRE(theme.metrics(theme::TypeStyle::Body).sizePx == 14);
        REQUIRE(theme.metrics(theme::TypeStyle::Body).lineHeightPx == 20);
        REQUIRE_FALSE(theme.fontFamilies(theme::FontKey::FontFamilyUi).isEmpty());
    }
    Theme dark;
    REQUIRE(dark.load(themePath("theme-dark.json")));
    REQUIRE(dark.isDark());
    REQUIRE(dark.shape(theme::Shape::ControlRadius) == 8.0);
    REQUIRE(dark.color(theme::Token::BackgroundHoverColor).alpha() < 255); // #RRGGBBAA parsed as alpha
    Theme light;
    REQUIRE(light.load(themePath("theme-light.json")));
    REQUIRE_FALSE(light.isDark());
    REQUIRE_FALSE(light.shape(theme::Shape::IslandRadius).has_value()); // null in the light file
}

TEST_CASE("theme: palettes follow the role table and the two surfaces differ only in Window")
{
    Theme theme;
    REQUIRE(theme.load(themePath("theme-dark.json")));
    const QPalette canvas = theme.palette(Surface::Canvas);
    const QPalette panel = theme.palette(Surface::Panel);
    REQUIRE(canvas.color(QPalette::Window) == theme.color(theme::Token::BackgroundCanvasColor));
    REQUIRE(panel.color(QPalette::Window) == theme.color(theme::Token::BackgroundLayer1Color));
    REQUIRE(canvas.color(QPalette::Base) == panel.color(QPalette::Base));
    REQUIRE(canvas.color(QPalette::Highlight) == theme.color(theme::Token::AccentBackgroundColorDefault));
    REQUIRE(canvas.color(QPalette::Inactive, QPalette::Highlight)
            == theme.color(theme::Token::AccentBackgroundColorInactive));
    REQUIRE(canvas.color(QPalette::Disabled, QPalette::Text) == theme.color(theme::Token::ContentColorDisabled));
    REQUIRE(canvas.color(QPalette::Disabled, QPalette::Button) == canvas.color(QPalette::Active, QPalette::Button));
    const QFont body = theme.font(theme::TypeStyle::Body);
    REQUIRE(body.pixelSize() == 14);
    REQUIRE(theme.font(theme::TypeStyle::BodyStrong).weight() == QFont::DemiBold);
}

TEST_CASE("theme: style and icons come up on the theme")
{
    Theme theme;
    REQUIRE(theme.load(themePath("theme-dark.json")));
    const CopyCoachStyle style(theme);
    REQUIRE(style.standardPalette().color(QPalette::Window) == theme.color(theme::Token::BackgroundCanvasColor));
    const Icons icons(theme);
    REQUIRE(Icons::exists(QStringLiteral("play")));
    REQUIRE_FALSE(Icons::exists(QStringLiteral("no-such-icon")));
    const QIcon play = icons.icon(QStringLiteral("play"));
    REQUIRE_FALSE(play.isNull());
    const QPixmap pixmap = play.pixmap(16, 16);
    REQUIRE(pixmap.width() == 16);
    REQUIRE(icons.icon(QStringLiteral("no-such-icon")).isNull());
}
