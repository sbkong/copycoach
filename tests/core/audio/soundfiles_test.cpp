// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// WAV reader, sound catalog and the generated metronome sets (audio-engine.md section 7).
#include "core/audio/catalog.h"
#include "core/io/wavfile.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace cc;

namespace
{

std::filesystem::path tempDir()
{
    const auto dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "wav";
    std::filesystem::create_directories(dir);
    return dir;
}

void put32(std::vector<char> &out, std::uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((v >> (8U * i)) & 0xFFU));
    }
}

void put16(std::vector<char> &out, std::uint16_t v)
{
    const auto wide = static_cast<std::uint32_t>(v);
    out.push_back(static_cast<char>(wide & 0xFFU));
    out.push_back(static_cast<char>((wide >> 8U) & 0xFFU));
}

/// Stereo PCM16 WAV: left = ramp 0, 0.25, 0.5, 0.75 ; right = -0.5 everywhere.
std::filesystem::path writeStereo16(const std::filesystem::path &dir)
{
    const std::vector<std::int16_t> frames{0, -16384, 8192, -16384, 16384, -16384, 24576, -16384};
    std::vector<char> bytes;
    const auto dataSize = static_cast<std::uint32_t>(frames.size() * 2);
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    put32(bytes, 36 + dataSize);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(bytes, 16);
    put16(bytes, 1);
    put16(bytes, 2);
    put32(bytes, 44100);
    put32(bytes, 44100 * 4);
    put16(bytes, 4);
    put16(bytes, 16);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    put32(bytes, dataSize);
    for (const std::int16_t s : frames) {
        put16(bytes, static_cast<std::uint16_t>(s));
    }
    const auto path = dir / "stereo16.wav";
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("wav: reads PCM16 stereo planar and reports errors as Result")
{
    const auto path = writeStereo16(tempDir());
    const auto clip = io::readWavFile(path);
    REQUIRE(clip);
    REQUIRE(clip->sampleRate == 44100.0);
    REQUIRE(clip->channels.size() == 2);
    REQUIRE(clip->frames() == 4);
    REQUIRE(clip->channels[0][2] == 0.5F);
    REQUIRE(clip->channels[1][3] == -0.5F);
    const auto mono = io::toMono(*clip);
    REQUIRE(mono[2] == 0.0F);

    REQUIRE(io::readWavFile(tempDir() / "missing.wav").error().code == ErrorCode::FileNotFound);
    std::ofstream(tempDir() / "junk.wav", std::ios::binary) << "not a wav";
    REQUIRE(io::readWavFile(tempDir() / "junk.wav").error().code == ErrorCode::FileCorrupt);
}

TEST_CASE("wav: linear resampling keeps endpoints and interpolates")
{
    const std::vector<float> ramp{0.0F, 1.0F, 2.0F, 3.0F};
    const auto up = io::resampleLinear(ramp, 1000.0, 2000.0);
    REQUIRE(up.size() == 7);
    REQUIRE(up[1] == 0.5F);
    REQUIRE(up[6] == 3.0F);
    REQUIRE(io::resampleLinear(ramp, 1000.0, 1000.0) == ramp);
}

TEST_CASE("catalog: bundled catalog loads, ids are well formed, every metronome set is generated")
{
    const auto catalog = audio::loadSoundCatalog(std::filesystem::path(CC_DATA_DIR) / "sounds" / "catalog.json");
    REQUIRE(catalog);
    REQUIRE(catalog->entries.size() == 11);
    const audio::SoundEntry *jazz = catalog->find("guitar-jazz");
    REQUIRE(jazz != nullptr);
    REQUIRE(jazz->kind == audio::SoundKind::Guitar);
    REQUIRE(jazz->preset);
    REQUIRE(jazz->preset.value_or(audio::SoundfontPreset{}).program == 26);
    REQUIRE(catalog->find("nope") == nullptr);

    int metronomes = 0;
    for (const audio::SoundEntry &entry : catalog->entries) {
        if (entry.kind != audio::SoundKind::Metronome) {
            continue;
        }
        ++metronomes;
        const auto set = audio::loadMetronomeSet(std::filesystem::path(CC_SOUND_DIR) / entry.dir, 48000.0);
        REQUIRE(set);
        REQUIRE(set->accent.size() > 100);
        REQUIRE(set->accent.size() <= 48000); // at most one second (audio-engine.md section 7)
        REQUIRE(set->beat.size() > 100);
        REQUIRE(set->sub.size() > 100);
    }
    REQUIRE(metronomes == 4);
}

TEST_CASE("catalog: rejects malformed ids")
{
    const auto dir = tempDir();
    std::ofstream(dir / "bad.json")
        << R"({"schema_version": 1, "sounds": [{"id": "Bad_Id", "kind": "metronome", "source": {"dir": "x"}}]})";
    const auto bad = audio::loadSoundCatalog(dir / "bad.json");
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().code == ErrorCode::FileCorrupt);
}
