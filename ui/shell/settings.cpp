// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/settings.h"

#include "core/io/jsonfile.h"

#include <optional>

namespace cc::ui
{

namespace
{

using nlohmann::json;

constexpr int settingsSchemaVersion = 1;
constexpr int layoutSchemaVersion = 1;

std::string keyString(setting::Key key)
{
    return std::string(setting::keyOf(key));
}

std::optional<setting::Key> keyFromString(const std::string &text)
{
    for (std::size_t i = 0; i < setting::infos.size(); ++i) {
        if (setting::infos[i].key == text) {
            return static_cast<setting::Key>(i);
        }
    }
    return std::nullopt;
}

std::optional<workspace::Id> workspaceFromString(const std::string &text)
{
    for (std::size_t i = 0; i < workspace::ids.size(); ++i) {
        if (workspace::ids[i] == text) {
            return static_cast<workspace::Id>(i);
        }
    }
    return std::nullopt;
}

std::string base64(const QByteArray &bytes)
{
    return bytes.toBase64().toStdString();
}

QByteArray fromBase64(const json &value)
{
    return value.is_string() ? QByteArray::fromBase64(QByteArray::fromStdString(value.get<std::string>()))
                             : QByteArray();
}

} // namespace

Settings::Settings(std::filesystem::path dir, QObject *parent)
    : QObject(parent)
    , m_dir(std::move(dir))
{
}

Result<void> Settings::load()
{
    if (auto r = loadSettings(); !r) {
        return r;
    }
    return loadLayout();
}

Result<void> Settings::loadSettings()
{
    const std::filesystem::path path = m_dir / "settings.json";
    if (!std::filesystem::exists(path)) {
        return {};
    }
    auto doc = io::readJsonFile(path);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    if (auto version = io::schemaVersionOf(*doc, settingsSchemaVersion, path); !version) {
        return std::unexpected(version.error());
    }
    for (const auto &[name, value] : doc->value("values", json::object()).items()) {
        if (const auto key = keyFromString(name)) {
            m_values[*key] = value;
        }
    }
    return {};
}

Result<void> Settings::loadLayout()
{
    const std::filesystem::path path = m_dir / "layout.json";
    if (!std::filesystem::exists(path)) {
        return {};
    }
    auto doc = io::readJsonFile(path);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    if (auto version = io::schemaVersionOf(*doc, layoutSchemaVersion, path); !version) {
        return std::unexpected(version.error());
    }
    m_geometry = fromBase64(doc->value("window", json::object()).value("geometry", json()));
    for (const auto &[name, value] : doc->value("layouts", json::object()).items()) {
        if (const auto id = workspaceFromString(name)) {
            m_layouts[*id] = fromBase64(value);
        }
    }
    return {};
}

Result<void> Settings::save() const
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    json values = json::object();
    for (const auto &[key, value] : m_values) {
        values[keyString(key)] = value;
    }
    const json settings{{"schema_version", settingsSchemaVersion}, {"values", values}};
    if (auto r = io::writeJsonFileAtomic(m_dir / "settings.json", settings); !r) {
        return r;
    }
    json layouts = json::object();
    for (const auto &[id, state] : m_layouts) {
        layouts[std::string(workspace::idOf(id))] = base64(state);
    }
    const json layout{{"schema_version", layoutSchemaVersion},
                      {"window", {{"geometry", base64(m_geometry)}}},
                      {"layouts", layouts}};
    return io::writeJsonFileAtomic(m_dir / "layout.json", layout);
}

json Settings::defaultValue(setting::Key key)
{
    return json::parse(setting::info(key).defaultJson, nullptr, false);
}

json Settings::value(setting::Key key) const
{
    const auto it = m_values.find(key);
    return it == m_values.end() ? defaultValue(key) : it->second;
}

bool Settings::boolValue(setting::Key key) const
{
    const json v = value(key);
    return v.is_boolean() && v.get<bool>();
}

int Settings::intValue(setting::Key key) const
{
    const json v = value(key);
    return v.is_number() ? v.get<int>() : 0;
}

QString Settings::stringValue(setting::Key key) const
{
    const json v = value(key);
    return v.is_string() ? QString::fromStdString(v.get<std::string>()) : QString();
}

void Settings::setValue(setting::Key key, const json &value)
{
    if (this->value(key) == value) {
        return;
    }
    if (value == defaultValue(key)) {
        m_values.erase(key);
    } else {
        m_values[key] = value;
    }
    emit valueChanged(key);
}

void Settings::setValue(setting::Key key, const QString &value)
{
    setValue(key, json(value.toStdString()));
}

QByteArray Settings::windowGeometry() const
{
    return m_geometry;
}

void Settings::setWindowGeometry(const QByteArray &geometry)
{
    m_geometry = geometry;
}

QByteArray Settings::layoutState(workspace::Id id) const
{
    const auto it = m_layouts.find(id);
    return it == m_layouts.end() ? QByteArray() : it->second;
}

void Settings::setLayoutState(workspace::Id id, const QByteArray &state)
{
    m_layouts[id] = state;
}

const std::filesystem::path &Settings::dir() const
{
    return m_dir;
}

} // namespace cc::ui
