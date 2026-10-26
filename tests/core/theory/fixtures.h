// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/theory/tuning.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Routine fixtures extracted from routine-process HTML (tools/verify/extract_fixtures.py). CC_FIXTURE_DIR comes from
// CMake.
namespace cc::test
{

inline nlohmann::json loadRoutineFixture(const std::string &name)
{
    const std::string path = std::string(CC_FIXTURE_DIR) + "/routine/" + name + ".json";
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("fixture not found: " + path);
    }
    return nlohmann::json::parse(in);
}

/// Note literals are [string, fret, label, role, diamond?]; role "D" marks a dimmed (not played) note.
inline std::vector<theory::StringFret> playedPositions(const nlohmann::json &notes)
{
    std::vector<theory::StringFret> out;
    for (const auto &note : notes) {
        if (note.size() >= 4 && note[3].is_string() && note[3].get<std::string>() == "D") {
            continue;
        }
        out.push_back(theory::StringFret{note[0].get<int>(), note[1].get<int>()});
    }
    return out;
}

/// "8 x 9 9 8 10" -> positions, string 6 first.
inline std::vector<theory::StringFret> positionsFromTab(const std::string &tab)
{
    std::vector<theory::StringFret> out;
    int string = 6;
    std::string token;
    for (std::size_t i = 0; i <= tab.size(); ++i) {
        if (i == tab.size() || tab[i] == ' ') {
            if (!token.empty()) {
                if (token != "x") {
                    out.push_back(theory::StringFret{string, std::stoi(token)});
                }
                --string;
                token.clear();
            }
        } else {
            token += tab[i];
        }
    }
    return out;
}

} // namespace cc::test
