// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/theme/theme.h"

#include "core/io/jsonfile.h"

#include <algorithm>
#include <string>

namespace cc::ui
{

namespace
{

using nlohmann::json;

constexpr int aliasDepthLimit = 8;
constexpr std::array<const char *, 3> colorSections{"primitive", "semantic", "component"};

/// "#RRGGBB" or "#RRGGBBAA" (CSS order, theme JSON `alpha-format`) to QColor.
std::optional<QColor> parseColor(const std::string &text)
{
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') {
        return std::nullopt;
    }
    bool ok = false;
    const auto value = QString::fromStdString(text.substr(1)).toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    if (text.size() == 7) {
        return QColor::fromRgb(value & 0xFFFFFFU);
    }
    return QColor::fromRgba(((value & 0xFFU) << 24U) | (value >> 8U));
}

/// The entry for a token name in any colour section, or null.
const json *findEntry(const json &doc, const std::string &name)
{
    for (const char *section : colorSections) {
        const auto table = doc.find(section);
        if (table != doc.end() && table->contains(name)) {
            return &(*table)[name];
        }
    }
    return nullptr;
}

/// Follows `$alias` chains until a hex string appears. nullopt for computed tokens and broken aliases.
std::optional<std::string> resolveColorText(const json &doc, std::string name)
{
    for (int depth = 0; depth <= aliasDepthLimit; ++depth) {
        const json *entry = findEntry(doc, name);
        if (entry == nullptr) {
            return std::nullopt;
        }
        if (entry->is_string()) {
            return entry->get<std::string>();
        }
        if (entry->is_object() && entry->contains("value") && (*entry)["value"].is_string()) {
            return (*entry)["value"].get<std::string>();
        }
        if (!entry->is_object() || !entry->contains("$alias")) {
            return std::nullopt;
        }
        name = (*entry)["$alias"].get<std::string>();
    }
    return std::nullopt;
}

bool isComputed(const json &doc, const std::string &name)
{
    const json *entry = findEntry(doc, name);
    return entry != nullptr && entry->is_object() && entry->contains("$computed");
}

std::optional<double> numberOrNull(const json &entry)
{
    if (entry.is_number()) {
        return entry.get<double>();
    }
    if (entry.is_object() && entry.contains("value") && entry["value"].is_number()) {
        return entry["value"].get<double>();
    }
    return std::nullopt;
}

QStringList familyList(const json &entry)
{
    QStringList list;
    for (const json &item : entry) {
        if (item.is_string()) {
            list.append(QString::fromStdString(item.get<std::string>()));
        }
    }
    return list;
}

template<typename Enum, typename Table>
std::optional<std::string> firstMissing(const json &section, const Table &names)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(Enum::Count); ++i) {
        if (!section.contains(std::string(names[i]))) {
            return std::string(names[i]);
        }
    }
    return std::nullopt;
}

/// The token's colour; an invalid QColor for `$computed` tokens; an error for missing names and bad values.
Result<QColor> tokenColor(const json &doc, const std::string &name, const std::filesystem::path &path)
{
    const auto text = resolveColorText(doc, name);
    if (!text) {
        if (isComputed(doc, name)) {
            return QColor();
        }
        return fail(ErrorCode::FileCorrupt, path.string(), "token not found or unresolved alias: " + name);
    }
    const auto color = parseColor(*text);
    if (!color) {
        return fail(ErrorCode::FileCorrupt, path.string(), "token " + name + " is not a hex color");
    }
    return *color;
}

} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
{
}

Result<void> Theme::loadColors(const nlohmann::json &doc, const std::filesystem::path &path)
{
    for (std::size_t i = 0; i < m_colors.size(); ++i) {
        const auto color = tokenColor(doc, std::string(theme::tokenNames[i]), path);
        if (!color) {
            return std::unexpected(color.error());
        }
        m_colors[i] = *color;
    }
    return {};
}

Result<void> Theme::loadMetrics(const nlohmann::json &doc, const std::filesystem::path &path)
{
    const json shape = doc.value("shape", json::object());
    const json spacing = doc.value("spacing", json::object());
    const json typography = doc.value("typography", json::object());
    const json font = doc.value("font", json::object());
    for (const auto &missing : {firstMissing<theme::Shape>(shape, theme::shapeNames),
                                firstMissing<theme::Spacing>(spacing, theme::spacingNames),
                                firstMissing<theme::TypeStyle>(typography, theme::typeStyleNames),
                                firstMissing<theme::FontKey>(font, theme::fontKeyNames)}) {
        if (missing) {
            return fail(ErrorCode::FileCorrupt, path.string(), "missing entry: " + *missing);
        }
    }
    for (std::size_t i = 0; i < m_shapes.size(); ++i) {
        m_shapes[i] = numberOrNull(shape[std::string(theme::shapeNames[i])]);
    }
    for (std::size_t i = 0; i < m_spacing.size(); ++i) {
        m_spacing[i] = spacing[std::string(theme::spacingNames[i])].get<int>();
    }
    for (std::size_t i = 0; i < m_metrics.size(); ++i) {
        const json &entry = typography[std::string(theme::typeStyleNames[i])];
        m_metrics[i] = TypeMetrics{entry.value("size", 14), entry.value("line-height", 20), entry.value("weight", 400)};
    }
    for (std::size_t i = 0; i + 1 < m_fonts.size(); ++i) { // the last key is the numbers feature, a string
        m_fonts[i] = familyList(font[std::string(theme::fontKeyNames[i])]);
    }
    const std::string numbersKey(theme::name(theme::FontKey::FontFeatureNumbers));
    m_numbersFeature = QString::fromStdString(font.value(numbersKey, "tnum"));
    return {};
}

Result<void> Theme::load(const std::filesystem::path &path)
{
    auto doc = io::readJsonFile(path);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    if (auto r = loadColors(*doc, path); !r) {
        return r;
    }
    if (auto r = loadMetrics(*doc, path); !r) {
        return r;
    }
    m_dark = doc->value("theme", "dark") == "dark";
    emit changed();
    return {};
}

bool Theme::isDark() const
{
    return m_dark;
}

QColor Theme::color(theme::Token token) const
{
    return m_colors[static_cast<std::size_t>(token)];
}

int Theme::spacing(theme::Spacing key) const
{
    return m_spacing[static_cast<std::size_t>(key)];
}

std::optional<double> Theme::shape(theme::Shape key) const
{
    return m_shapes[static_cast<std::size_t>(key)];
}

TypeMetrics Theme::metrics(theme::TypeStyle style) const
{
    return m_metrics[static_cast<std::size_t>(style)];
}

const QStringList &Theme::fontFamilies(theme::FontKey key) const
{
    return m_fonts[static_cast<std::size_t>(key)];
}

QString Theme::numbersFeature() const
{
    return m_numbersFeature;
}

QFont Theme::font(theme::TypeStyle style, bool tabularNumbers) const
{
    const TypeMetrics m = metrics(style);
    QFont f;
    f.setFamilies(fontFamilies(theme::FontKey::FontFamilyUi));
    f.setPixelSize(m.sizePx);
    f.setWeight(static_cast<QFont::Weight>(m.weight));
    if (const auto tag = QFont::Tag::fromString(m_numbersFeature); tabularNumbers && tag) {
        f.setFeature(*tag, 1);
    }
    return f;
}

QFont Theme::monoFont(theme::TypeStyle style) const
{
    QFont f = font(style);
    f.setFamilies(fontFamilies(theme::FontKey::FontFamilyMono));
    return f;
}

} // namespace cc::ui
