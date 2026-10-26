// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/metronome.h"
#include "core/error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// sounds/catalog.json (audio-engine.md section 7): the guitar, piano and metronome sounds the user can pick.
// Ids are persistent (saved in routine-state.json and projects), so they follow docs/ui/naming.md section 1.
namespace cc::audio
{

enum class SoundKind : std::uint8_t {
    Guitar,
    Piano,
    Metronome,
};

struct SoundfontPreset {
    std::string soundfont; // file name relative to the sounds folder
    int bank = 0;
    int program = 0;
};

struct SoundEntry {
    std::string id;
    SoundKind kind = SoundKind::Guitar;
    std::string label;
    std::optional<SoundfontPreset> preset; // guitar / piano
    std::string dir; // metronome: folder with accent.wav, beat.wav, sub.wav
    std::string license;
};

struct SoundCatalog {
    int schemaVersion = 1;
    std::vector<SoundEntry> entries;

    [[nodiscard]] const SoundEntry *find(const std::string &id) const;
};

constexpr int soundCatalogSchemaVersion = 1;

Result<SoundCatalog> loadSoundCatalog(const std::filesystem::path &path);

/// Loads the three clips of a metronome set, mixed to mono and resampled to `sampleRate`.
Result<MetronomeSet> loadMetronomeSet(const std::filesystem::path &dir, double sampleRate);

} // namespace cc::audio
