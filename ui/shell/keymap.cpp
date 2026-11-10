// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/keymap.h"

#include "core/io/jsonfile.h"
#include "generated/actionids.h"
#include "ui/shell/actionregistry.h"

#include <QAction>

namespace cc::ui
{

namespace
{

using nlohmann::json;

constexpr int keymapSchemaVersion = 1;

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

/// The registry row for an id, expanding "{n}" templates; nullopt for unknown ids.
struct Resolved {
    const action::Info *info = nullptr;
    int n = 0;
};

QString instanceId(const action::Info &info, int n)
{
    return QString(fromView(info.id)).replace(QStringLiteral("{n}"), QString::number(n));
}

/// The instance number of a template row that spells `actionId`, or nullopt.
std::optional<int> instanceOf(const action::Info &info, const QString &actionId)
{
    if (info.rangeMax == 0) {
        return fromView(info.id) == actionId ? std::optional<int>(0) : std::nullopt;
    }
    for (int n = info.rangeMin; n <= info.rangeMax; ++n) {
        if (instanceId(info, n) == actionId) {
            return n;
        }
    }
    return std::nullopt;
}

std::optional<Resolved> resolve(const QString &actionId)
{
    for (const action::Info &info : action::infos) {
        if (const auto n = instanceOf(info, actionId)) {
            return Resolved{&info, *n};
        }
    }
    return std::nullopt;
}

QList<QKeySequence> parseKeys(const json &array)
{
    QList<QKeySequence> keys;
    for (const json &item : array) {
        const QString text = item.is_string() ? QString::fromStdString(item.get<std::string>()) : QString();
        const QKeySequence key = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (!key.isEmpty()) {
            keys.append(key);
        }
    }
    return keys;
}

json keysToJson(const QList<QKeySequence> &keys)
{
    json array = json::array();
    for (const QKeySequence &key : keys) {
        array.push_back(key.toString(QKeySequence::PortableText).toStdString());
    }
    return array;
}

} // namespace

workspace::Context contextOfActionId(const QString &actionId)
{
    const auto resolved = resolve(actionId);
    if (!resolved) {
        return workspace::Context::Global;
    }
    for (std::size_t i = 0; i < workspace::contextIds.size(); ++i) {
        if (workspace::contextIds[i] == resolved->info->context) {
            return static_cast<workspace::Context>(i);
        }
    }
    return workspace::Context::Global;
}

Keymap::Keymap(std::filesystem::path dir, QObject *parent)
    : QObject(parent)
    , m_dir(std::move(dir))
{
}

Result<void> Keymap::load()
{
    const std::filesystem::path path = m_dir / "keymap.json";
    if (!std::filesystem::exists(path)) {
        return {};
    }
    auto doc = io::readJsonFile(path);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    if (auto version = io::schemaVersionOf(*doc, keymapSchemaVersion, path); !version) {
        return std::unexpected(version.error());
    }
    m_overrides.clear();
    for (const auto &[id, value] : doc->value("keys", json::object()).items()) {
        const QString current = currentId(QString::fromStdString(id));
        if (value.is_array() && !current.isEmpty()) { // a retired id (legacy without successor) is dropped
            m_overrides[current] = parseKeys(value);
        }
    }
    return {};
}

/// A renamed action keeps its override under the new id (registry legacy table, naming.md section 1 rule 3);
/// a retired one (empty successor) maps to the empty string.
QString Keymap::currentId(const QString &id)
{
    for (const action::Legacy &entry : action::legacy) {
        if (id == QString::fromUtf8(entry.oldId.data(), static_cast<qsizetype>(entry.oldId.size()))) {
            return QString::fromUtf8(entry.newId.data(), static_cast<qsizetype>(entry.newId.size()));
        }
    }
    return id;
}

Result<void> Keymap::save() const
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    json keys = json::object();
    for (const auto &[id, list] : m_overrides) {
        keys[id.toStdString()] = keysToJson(list);
    }
    const json doc{{"schema_version", keymapSchemaVersion},
                   {"preset", m_overrides.empty() ? "gp8" : "user"},
                   {"keys", keys}};
    return io::writeJsonFileAtomic(m_dir / "keymap.json", doc);
}

QList<QKeySequence> Keymap::defaultKeys(const QString &actionId)
{
    const auto resolved = resolve(actionId);
    if (!resolved) {
        return {};
    }
    QList<QKeySequence> keys;
    for (std::size_t k = 0; k < resolved->info->keyCount; ++k) {
        QString text = fromView(resolved->info->defaultKeys[k]);
        text.replace(QStringLiteral("{n}"), QString::number(resolved->n));
        if (!text.isEmpty()) {
            keys.append(QKeySequence::fromString(text, QKeySequence::PortableText));
        }
    }
    return keys;
}

QList<QKeySequence> Keymap::keys(const QString &actionId) const
{
    const auto it = m_overrides.find(actionId);
    return it == m_overrides.end() ? defaultKeys(actionId) : it->second;
}

bool Keymap::isDefault(const QString &actionId) const
{
    return !m_overrides.contains(actionId);
}

int Keymap::overrideCount() const
{
    return static_cast<int>(m_overrides.size());
}

void Keymap::setKeys(const QString &actionId, const QList<QKeySequence> &keys)
{
    if (keys == defaultKeys(actionId)) {
        m_overrides.erase(actionId);
    } else {
        m_overrides[actionId] = keys;
    }
    emit keysChanged(actionId);
}

void Keymap::resetAll()
{
    std::vector<QString> ids;
    ids.reserve(m_overrides.size());
    for (const auto &[id, list] : m_overrides) {
        ids.push_back(id);
    }
    m_overrides.clear();
    for (const QString &id : ids) {
        emit keysChanged(id);
    }
}

namespace
{

/// Whether two actions can collide: same context, or one of them global (a workspace key shadows a global one).
bool contextsMeet(workspace::Context a, workspace::Context b)
{
    return a == b || a == workspace::Context::Global || b == workspace::Context::Global;
}

/// The registry row expanded to its instance ids (one id for plain rows, rangeMin..rangeMax for templates).
std::vector<QString> instanceIds(const action::Info &info)
{
    std::vector<QString> ids;
    const int first = info.rangeMax == 0 ? 0 : info.rangeMin;
    const int last = info.rangeMax == 0 ? 0 : info.rangeMax;
    ids.reserve(static_cast<std::size_t>(last - first) + 1);
    for (int n = first; n <= last; ++n) {
        ids.push_back(instanceId(info, n));
    }
    return ids;
}

QString instanceLabel(const action::Info &info, const QString &id)
{
    const QString number = id.mid(QString(fromView(info.id)).indexOf(QStringLiteral("{n}")));
    return QString(fromView(info.label)).replace(QStringLiteral("{n}"), number);
}

} // namespace

std::vector<KeyConflict> Keymap::conflicts(const QString &actionId, const QKeySequence &key) const
{
    std::vector<KeyConflict> found;
    if (key.isEmpty()) {
        return found;
    }
    for (const action::Info &info : action::infos) {
        collectConflicts(info, actionId, key, found);
    }
    return found;
}

/// Appends the instances of one registry row that hold `key` and could shadow or be shadowed by `actionId`.
void Keymap::collectConflicts(const action::Info &info,
                              const QString &actionId,
                              const QKeySequence &key,
                              std::vector<KeyConflict> &found) const
{
    const workspace::Context own = contextOfActionId(actionId);
    for (const QString &id : instanceIds(info)) {
        const bool taken = id != actionId && contextsMeet(own, contextOfActionId(id)) && keys(id).contains(key);
        if (taken) {
            found.push_back(KeyConflict{id, instanceLabel(info, id), key});
        }
    }
}

void Keymap::apply(ActionRegistry &registry) const
{
    for (QAction *action : registry.all()) {
        action->setShortcuts(keys(action->objectName()));
    }
}

} // namespace cc::ui
