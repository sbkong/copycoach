// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "generated/settingkeys.h"
#include "generated/workspaceids.h"

#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QObject>
#include <QString>

#include <filesystem>
#include <map>

// settings.json and layout.json in the app data folder (docs/ui/naming.md section 5). No QSettings. Values that
// equal the registry default are not written. keymap.json comes with the keymap editor.
namespace cc::ui
{

class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(std::filesystem::path dir, QObject *parent = nullptr);

    /// Reads both files; a missing file means defaults, a corrupt one is reported and ignored.
    [[nodiscard]] Result<void> load();
    [[nodiscard]] Result<void> save() const;

    [[nodiscard]] nlohmann::json value(setting::Key key) const;
    [[nodiscard]] static nlohmann::json defaultValue(setting::Key key);
    [[nodiscard]] bool boolValue(setting::Key key) const;
    [[nodiscard]] int intValue(setting::Key key) const;
    [[nodiscard]] QString stringValue(setting::Key key) const;
    void setValue(setting::Key key, const nlohmann::json &value);
    void setValue(setting::Key key, const QString &value);

    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);
    [[nodiscard]] QByteArray layoutState(workspace::Id id) const;
    void setLayoutState(workspace::Id id, const QByteArray &state);

    [[nodiscard]] const std::filesystem::path &dir() const;

signals:
    void valueChanged(cc::setting::Key key);

private:
    [[nodiscard]] Result<void> loadSettings();
    [[nodiscard]] Result<void> loadLayout();

    std::filesystem::path m_dir;
    std::map<setting::Key, nlohmann::json> m_values; // only keys that differ from the default
    QByteArray m_geometry;
    std::map<workspace::Id, QByteArray> m_layouts;
};

} // namespace cc::ui
