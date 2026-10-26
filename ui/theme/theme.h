// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "generated/tokens.h"

#include <nlohmann/json_fwd.hpp>

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPalette>
#include <QStringList>

#include <array>
#include <filesystem>
#include <optional>

// The one theme object (PLAN 4-3 decision 1): every color, radius, spacing and type style a painter or palette
// needs comes from here, read from data/ui/theme-<name>.json. Values never appear in code.
namespace cc::ui
{

enum class Surface : std::uint8_t {
    Canvas, // window chrome: menu bar, toolbar, status bar, dialogs (qpalette.md section 1)
    Panel, // dock contents
};

struct TypeMetrics {
    int sizePx = 14;
    int lineHeightPx = 20;
    int weight = 400;
};

class Theme : public QObject
{
    Q_OBJECT
public:
    explicit Theme(QObject *parent = nullptr);

    /// Replaces every token from the file and emits changed(). FileCorrupt names the first bad token.
    [[nodiscard]] Result<void> load(const std::filesystem::path &path);

    [[nodiscard]] bool isDark() const;
    [[nodiscard]] QColor color(theme::Token token) const;
    [[nodiscard]] int spacing(theme::Spacing key) const;
    [[nodiscard]] std::optional<double> shape(theme::Shape key) const;
    [[nodiscard]] TypeMetrics metrics(theme::TypeStyle style) const;
    [[nodiscard]] const QStringList &fontFamilies(theme::FontKey key) const;
    [[nodiscard]] QString numbersFeature() const;

    /// A QFont for a type ramp entry in the UI family stack; `tabularNumbers` turns on the tnum feature.
    [[nodiscard]] QFont font(theme::TypeStyle style, bool tabularNumbers = false) const;
    [[nodiscard]] QFont monoFont(theme::TypeStyle style) const;
    /// Full palette (all roles, all three groups) for a surface (qpalette.md sections 2 and 3).
    [[nodiscard]] QPalette palette(Surface surface) const;

signals:
    void changed();

private:
    [[nodiscard]] Result<void> loadColors(const nlohmann::json &doc, const std::filesystem::path &path);
    [[nodiscard]] Result<void> loadMetrics(const nlohmann::json &doc, const std::filesystem::path &path);

    bool m_dark = true;
    std::array<QColor, static_cast<std::size_t>(theme::Token::Count)> m_colors{};
    std::array<std::optional<double>, static_cast<std::size_t>(theme::Shape::Count)> m_shapes{};
    std::array<int, static_cast<std::size_t>(theme::Spacing::Count)> m_spacing{};
    std::array<TypeMetrics, static_cast<std::size_t>(theme::TypeStyle::Count)> m_metrics{};
    std::array<QStringList, static_cast<std::size_t>(theme::FontKey::Count)> m_fonts{};
    QString m_numbersFeature;
};

} // namespace cc::ui
