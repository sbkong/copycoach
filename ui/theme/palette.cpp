// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Theme::palette: the QPalette role table of docs/ui/qpalette.md sections 2 and 3. Every role is set
// explicitly so Qt never derives a grey of its own.
#include "ui/theme/theme.h"

namespace cc::ui
{

namespace
{

using theme::Token;

void setAllGroups(QPalette &palette, QPalette::ColorRole role, const QColor &color)
{
    palette.setColor(QPalette::Active, role, color);
    palette.setColor(QPalette::Inactive, role, color);
    palette.setColor(QPalette::Disabled, role, color);
}

void setActiveRoles(QPalette &palette, const Theme &theme, Surface surface)
{
    const Token window = surface == Surface::Canvas ? Token::BackgroundCanvasColor : Token::BackgroundLayer1Color;
    setAllGroups(palette, QPalette::Window, theme.color(window));
    setAllGroups(palette, QPalette::WindowText, theme.color(Token::ContentColorDefault));
    setAllGroups(palette, QPalette::Base, theme.color(Token::BackgroundInlineColor));
    setAllGroups(palette, QPalette::AlternateBase, theme.color(Token::BackgroundLayer1Color));
    setAllGroups(palette, QPalette::Text, theme.color(Token::ContentColorDefault));
    setAllGroups(palette, QPalette::PlaceholderText, theme.color(Token::ContentColorTertiary));
    setAllGroups(palette, QPalette::Button, theme.color(Token::BackgroundLayer3Color));
    setAllGroups(palette, QPalette::ButtonText, theme.color(Token::ContentColorDefault));
    setAllGroups(palette, QPalette::BrightText, theme.color(Token::AccentContentColorOnAccent));
    setAllGroups(palette, QPalette::Highlight, theme.color(Token::AccentBackgroundColorDefault));
    setAllGroups(palette, QPalette::HighlightedText, theme.color(Token::AccentContentColorOnBackground));
    setAllGroups(palette, QPalette::Accent, theme.color(Token::AccentColorDefault));
    setAllGroups(palette, QPalette::Link, theme.color(Token::ContentColorLink));
    setAllGroups(palette, QPalette::LinkVisited, theme.color(Token::ContentColorLink));
    setAllGroups(palette, QPalette::ToolTipBase, theme.color(Token::TooltipBackgroundColor));
    setAllGroups(palette, QPalette::ToolTipText, theme.color(Token::TooltipContentColor));
    setAllGroups(palette, QPalette::Light, theme.color(Token::BackgroundLayer3Color));
    setAllGroups(palette, QPalette::Midlight, theme.color(Token::BackgroundLayer3Color));
    setAllGroups(palette, QPalette::Mid, theme.color(Token::BorderColorEmphasis));
    setAllGroups(palette, QPalette::Dark, theme.color(Token::BorderColorEmphasis));
    setAllGroups(palette, QPalette::Shadow, theme.color(Token::OverlayScrimColor));
}

void setInactiveAndDisabled(QPalette &palette, const Theme &theme)
{
    palette.setColor(QPalette::Inactive, QPalette::Highlight, theme.color(Token::AccentBackgroundColorInactive));
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, theme.color(Token::ContentColorDefault));
    const QColor disabled = theme.color(Token::ContentColorDisabled);
    for (const QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText}) {
        palette.setColor(QPalette::Disabled, role, disabled);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, theme.color(Token::AccentBackgroundColorInactive));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, theme.color(Token::ContentColorSecondary));
}

} // namespace

QPalette Theme::palette(Surface surface) const
{
    QPalette palette;
    setActiveRoles(palette, *this, surface);
    setInactiveAndDisabled(palette, *this);
    return palette;
}

} // namespace cc::ui
