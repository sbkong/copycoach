// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/id.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Field readers for the project parsers (coding-standards.md section 19: validate what we know, ignore the
// rest, bound everything). A bad field throws Corrupt; the public entry points catch it at their boundary and
// return FileCorrupt, so nothing escapes. A missing field takes `fallback`, or is an error without one.
namespace cc::project::jsonread
{

struct Corrupt : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline std::int64_t intAt(const nlohmann::json &node,
                          const char *name,
                          std::int64_t lo,
                          std::int64_t hi,
                          std::optional<std::int64_t> fallback = std::nullopt)
{
    const auto it = node.find(name);
    if (it == node.end()) {
        if (fallback) {
            return *fallback;
        }
        throw Corrupt{std::string(name) + " missing"};
    }
    if (!it->is_number_integer()) {
        throw Corrupt{std::string(name) + " is not an integer"};
    }
    const auto value = it->get<std::int64_t>();
    if (value < lo || value > hi) {
        throw Corrupt{std::string(name) + " out of range"};
    }
    return value;
}

inline double numberAt(const nlohmann::json &node,
                       const char *name,
                       double lo,
                       double hi,
                       std::optional<double> fallback = std::nullopt)
{
    const auto it = node.find(name);
    if (it == node.end()) {
        if (fallback) {
            return *fallback;
        }
        throw Corrupt{std::string(name) + " missing"};
    }
    if (!it->is_number()) {
        throw Corrupt{std::string(name) + " is not a number"};
    }
    const auto value = it->get<double>();
    if (std::isnan(value) || value < lo || value > hi) {
        throw Corrupt{std::string(name) + " out of range"};
    }
    return value;
}

inline std::string strAt(const nlohmann::json &node,
                         const char *name,
                         std::size_t maxBytes,
                         std::optional<std::string> fallback = std::nullopt)
{
    const auto it = node.find(name);
    if (it == node.end()) {
        if (fallback) {
            return *fallback;
        }
        throw Corrupt{std::string(name) + " missing"};
    }
    if (!it->is_string()) {
        throw Corrupt{std::string(name) + " is not a string"};
    }
    auto value = it->get<std::string>();
    if (value.size() > maxBytes) {
        throw Corrupt{std::string(name) + " too long"};
    }
    return value;
}

inline bool boolAt(const nlohmann::json &node, const char *name, bool fallback)
{
    const auto it = node.find(name);
    if (it == node.end()) {
        return fallback;
    }
    if (!it->is_boolean()) {
        throw Corrupt{std::string(name) + " is not a boolean"};
    }
    return it->get<bool>();
}

/// The array field, or an empty array when absent. More than `maxItems` entries is an error.
inline const nlohmann::json &arrayAt(const nlohmann::json &node, const char *name, std::size_t maxItems)
{
    static const nlohmann::json empty = nlohmann::json::array();
    const auto it = node.find(name);
    if (it == node.end()) {
        return empty;
    }
    if (!it->is_array()) {
        throw Corrupt{std::string(name) + " is not an array"};
    }
    if (it->size() > maxItems) {
        throw Corrupt{std::string(name) + " has too many entries"};
    }
    return *it;
}

/// The required "id" field, in the generated shape (model/id.h).
inline model::Id idAt(const nlohmann::json &node)
{
    auto id = strAt(node, "id", 64);
    if (!model::isWellFormedId(id)) {
        throw Corrupt{"id is not well formed: " + id};
    }
    return id;
}

/// Optional string field read as a stable id; empty and absent both mean "none".
inline std::optional<model::Id> optionalIdAt(const nlohmann::json &node, const char *name)
{
    auto id = strAt(node, name, 64, "");
    if (id.empty()) {
        return std::nullopt;
    }
    if (!model::isWellFormedId(id)) {
        throw Corrupt{std::string(name) + " is not well formed: " + id};
    }
    return id;
}

template<typename Enum, std::size_t N>
std::string_view nameOf(const std::array<std::pair<Enum, std::string_view>, N> &names, Enum value)
{
    for (const auto &[e, name] : names) {
        if (e == value) {
            return name;
        }
    }
    return names.front().second;
}

/// An enum field by its file name; `fallback` when absent, an error when the name is unknown.
template<typename Enum, std::size_t N>
Enum nameAt(const nlohmann::json &node,
            const char *name,
            const std::array<std::pair<Enum, std::string_view>, N> &names,
            Enum fallback)
{
    const auto it = node.find(name);
    if (it == node.end()) {
        return fallback;
    }
    if (!it->is_string()) {
        throw Corrupt{std::string(name) + " is not a string"};
    }
    const auto text = it->get<std::string>();
    for (const auto &[e, candidate] : names) {
        if (candidate == text) {
            return e;
        }
    }
    throw Corrupt{std::string(name) + " has an unknown value: " + text};
}

} // namespace cc::project::jsonread
