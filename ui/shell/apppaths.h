// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <filesystem>

// Where the app finds its bundled data and keeps the user's files. Bundled data sits next to the executable
// (data/, sounds/); user files live in QStandardPaths::AppDataLocation (naming.md section 5).
namespace cc::ui
{

struct AppPaths {
    std::filesystem::path dataDir; // <exe>/data: ui/theme-*.json, routines/default.json, sounds/catalog.json
    std::filesystem::path soundDir; // <exe>/sounds: metronome sets and the sound font
    std::filesystem::path userDir; // settings.json, layout.json, routine-state.json

    [[nodiscard]] static AppPaths detect();
    [[nodiscard]] std::filesystem::path theme(bool dark) const;
    [[nodiscard]] std::filesystem::path verovioDir() const; // fonts and glyph tables for the score engine
};

} // namespace cc::ui
