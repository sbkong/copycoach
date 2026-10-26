// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// FluidSynth node with the bundled sound font (audio-engine.md section 7): every catalog preset resolves, notes
// make sound, the render is deterministic. Skipped when the build was configured without the download.
#include "core/audio/catalog.h"
#include "core/audio/command.h"
#include "core/audio/fluidsynthnode.h"
#include "core/audio/rt/audioblock.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using namespace cc;
using namespace cc::audio;

namespace
{

constexpr double rate = 48000.0;

std::filesystem::path soundfontPath()
{
    return std::filesystem::path(CC_SOUND_DIR) / "MuseScore_General.sf3";
}

std::vector<float> renderLeft(ISynth &synth, int blocks)
{
    rt::BlockBuffer buffer;
    std::vector<float> left;
    for (int i = 0; i < blocks; ++i) {
        rt::AudioBlock block = buffer.block(rt::blockFrames);
        synth.process(block);
        left.insert(left.end(), block.channel(0).begin(), block.channel(0).end());
    }
    return left;
}

float peak(const std::vector<float> &samples)
{
    float p = 0.0F;
    for (const float s : samples) {
        p = std::max(p, std::abs(s));
    }
    return p;
}

} // namespace

TEST_CASE("fluidsynth: missing sound font is reported, not crashed on")
{
    FluidSynthNode synth(rate);
    const auto missing = synth.loadSoundfont(std::filesystem::path(CC_SOUND_DIR) / "nope.sf3");
    REQUIRE_FALSE(missing);
    REQUIRE(missing.error().code == ErrorCode::ModelMissing);
    REQUIRE(synth.selectPreset(0, SoundfontPreset{"nope.sf3", 0, 0}).error().code == ErrorCode::ModelMissing);
    synth.noteOn(0, 60, 1.0F); // no font: silent, no crash
    REQUIRE(peak(renderLeft(synth, 4)) == 0.0F);
}

TEST_CASE("fluidsynth: bundled sound font loads and every catalog preset resolves")
{
    if (!std::filesystem::exists(soundfontPath())) {
        SKIP("MuseScore_General.sf3 not downloaded (CC_DOWNLOAD_SOUNDFONT=OFF)");
    }
    FluidSynthNode synth(rate);
    REQUIRE(synth.loadSoundfont(soundfontPath()));
    const auto catalog = loadSoundCatalog(std::filesystem::path(CC_DATA_DIR) / "sounds" / "catalog.json");
    REQUIRE(catalog);
    for (const SoundEntry &entry : catalog->entries) {
        if (entry.preset) {
            INFO(entry.id);
            REQUIRE(synth.selectPreset(entry.kind == SoundKind::Guitar ? guitarChannel : pianoChannel, *entry.preset));
        }
    }
    REQUIRE(synth.selectPreset(0, SoundfontPreset{"MuseScore_General.sf3", 99, 99}).error().code
            == ErrorCode::ModelCorrupt);
}

TEST_CASE("fluidsynth: a note sounds, stops after note off, and renders the same twice")
{
    if (!std::filesystem::exists(soundfontPath())) {
        SKIP("MuseScore_General.sf3 not downloaded (CC_DOWNLOAD_SOUNDFONT=OFF)");
    }
    const auto run = [] {
        FluidSynthNode synth(rate);
        REQUIRE(synth.loadSoundfont(soundfontPath()));
        REQUIRE(synth.selectPreset(guitarChannel, SoundfontPreset{"MuseScore_General.sf3", 0, 26}));
        REQUIRE(peak(renderLeft(synth, 8)) == 0.0F);
        synth.noteOn(guitarChannel, 60, 0.8F);
        std::vector<float> held = renderLeft(synth, 100); // 6400 samples = 133 ms
        synth.noteOff(guitarChannel, 60);
        std::vector<float> released = renderLeft(synth, 3000); // 4 s: release has long faded
        REQUIRE(peak(held) > 0.01F);
        REQUIRE(peak(held) < 1.0F);
        REQUIRE(peak(std::vector<float>(released.end() - 6400, released.end())) < 1e-4F);
        held.insert(held.end(), released.begin(), released.end());
        return held;
    };
    REQUIRE(run() == run());
}
