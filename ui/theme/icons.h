// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QIcon>
#include <QString>

// Lucide icons (components.md section 3): SVG from the resource bundle, `currentColor` replaced by the token for
// the icon mode (default / disabled / on), stroke width 1.5, rendered at the requested size and device ratio.
namespace cc::ui
{

class Theme;

class Icons
{
public:
    explicit Icons(const Theme &theme);

    /// Icon by Lucide file name ("play", "skip-back"). Unknown names give a null icon.
    [[nodiscard]] QIcon icon(const QString &name) const;
    [[nodiscard]] static bool exists(const QString &name);

private:
    const Theme *m_theme;
};

} // namespace cc::ui
