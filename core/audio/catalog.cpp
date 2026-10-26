// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "core/audio/catalog.h"

#include "core/io/jsonfile.h"
#include "core/io/wavfile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace cc::audio
{

namespace
{

using nlohmann::json;

bool wellFormedId(const std::string &id)
{
    if (id.empty() || id.front() == '-' || id.back() == '-') {
        return false;
    }
    return std::ranges::all_of(id, [](unsigned char c) {
        return std::islower(c) != 0 || std::isdigit(c) != 0 || c == '-';
    });
}

std::optional<SoundKind> parseKind(const std::string &kind)
{
    if (kind == "guitar") {
        return SoundKind::Guitar;
    }
    if (kind == "piano") {
        return SoundKind::Piano;
    }
    if (kind == "metronome") {
        return SoundKind::Metronome;
    }
    return std::nullopt;
}

Result<SoundEntry> parseEntry(const json &node, const std::filesystem::path &path)
{
    const std::string id = node.value("id", "");
    if (!wellFormedId(id)) {
        return fail(ErrorCode::FileCorrupt, path.string(), "sound id must be lowercase words joined by hyphens: " + id);
    }
    const auto kind = parseKind(node.value("kind", ""));
    if (!kind) {
        return fail(ErrorCode::FileCorrupt, path.string(), "sound " + id + " has an unknown kind");
    }
    SoundEntry entry{id, *kind, node.value("label", id), std::nullopt, "", node.value("license", "")};
    const json source = node.value("source", json::object());
    if (*kind == SoundKind::Metronome) {
        entry.dir = source.value("dir", "");
        if (entry.dir.empty()) {
            return fail(ErrorCode::FileCorrupt, path.string(), "metronome sound " + id + " needs source.dir");
        }
        return entry;
    }
    if (!source.contains("soundfont") || !source.contains("program")) {
        return fail(ErrorCode::FileCorrupt, path.string(), "sound " + id + " needs source.soundfont and program");
    }
    entry.preset = SoundfontPreset{source["soundfont"], source.value("bank", 0), source["program"]};
    return entry;
}

Result<std::vector<float>> loadClip(const std::filesystem::path &file, double sampleRate)
{
    auto clip = io::readWavFile(file);
    if (!clip) {
        return std::unexpected(clip.error());
    }
    return io::resampleLinear(io::toMono(*clip), clip->sampleRate, sampleRate);
}

} // namespace

const SoundEntry *SoundCatalog::find(const std::string &id) const
{
    const auto it = std::ranges::find(entries, id, &SoundEntry::id);
    return it == entries.end() ? nullptr : &*it;
}

Result<SoundCatalog> loadSoundCatalog(const std::filesystem::path &path)
{
    auto document = io::readJsonFile(path);
    if (!document) {
        return std::unexpected(document.error());
    }
    const auto version = io::schemaVersionOf(*document, soundCatalogSchemaVersion, path);
    if (!version) {
        return std::unexpected(version.error());
    }
    SoundCatalog catalog;
    catalog.schemaVersion = *version;
    const json sounds = document->value("sounds", json());
    if (!sounds.is_array() || sounds.empty()) {
        return fail(ErrorCode::FileCorrupt, path.string(), "sounds missing");
    }
    for (const json &node : sounds) {
        auto entry = parseEntry(node, path);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        if (catalog.find(entry->id) != nullptr) {
            return fail(ErrorCode::FileCorrupt, path.string(), "duplicate sound id " + entry->id);
        }
        catalog.entries.push_back(std::move(*entry));
    }
    return catalog;
}

Result<MetronomeSet> loadMetronomeSet(const std::filesystem::path &dir, double sampleRate)
{
    MetronomeSet set;
    auto accent = loadClip(dir / "accent.wav", sampleRate);
    auto beat = loadClip(dir / "beat.wav", sampleRate);
    auto sub = loadClip(dir / "sub.wav", sampleRate);
    for (const auto *clip : {&accent, &beat, &sub}) {
        if (!*clip) {
            return std::unexpected(clip->error());
        }
    }
    set.accent = std::move(*accent);
    set.beat = std::move(*beat);
    set.sub = std::move(*sub);
    return set;
}

} // namespace cc::audio
