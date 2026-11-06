// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Key and tuning reference (copy-spec.md section 4 "키 · 센트"): synthesised triads with harmonics, played in
// a key and at a tuning offset, come back as that key and that offset.
#include "core/media/audiofile.h"
#include "core/predict/keycents.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace cc;
using namespace cc::predict;

namespace
{

constexpr int rate = keyCentsSampleRate;

/// A chord (midi notes) for `seconds` with four decaying harmonics, A4 at `a4Hz`.
void addChord(std::vector<float> &out, std::size_t start, double seconds, std::initializer_list<int> midis, double a4Hz)
{
    const auto n = static_cast<std::size_t>(seconds * rate);
    for (std::size_t i = 0; i < n && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        double v = 0.0;
        for (const int midi : midis) {
            const double f0 = a4Hz * std::pow(2.0, (midi - 69) / 12.0);
            for (int h = 1; h <= 4; ++h) {
                v += std::sin(2.0 * std::numbers::pi * f0 * h * t) / (h * h);
            }
        }
        out[start + i] += static_cast<float>(0.1 * v);
    }
}

/// I IV V I in C major (or i iv V i in A minor) over eight seconds.
std::vector<float> progression(bool minor, double a4Hz)
{
    std::vector<float> mono(static_cast<std::size_t>(rate) * 8, 0.0F);
    const auto at = [](int bar) {
        return static_cast<std::size_t>(bar) * 2 * rate;
    };
    if (!minor) {
        addChord(mono, at(0), 2.0, {48, 52, 55, 60}, a4Hz); // C E G c
        addChord(mono, at(1), 2.0, {53, 57, 60, 65}, a4Hz); // F A c f
        addChord(mono, at(2), 2.0, {55, 59, 62, 67}, a4Hz); // G B d g
        addChord(mono, at(3), 2.0, {48, 52, 55, 60}, a4Hz);
    } else {
        addChord(mono, at(0), 2.0, {45, 48, 52, 57}, a4Hz); // A C E a
        addChord(mono, at(1), 2.0, {50, 53, 57, 62}, a4Hz); // D F A d
        addChord(mono, at(2), 2.0, {52, 56, 59, 64}, a4Hz); // E G# B e
        addChord(mono, at(3), 2.0, {45, 48, 52, 57}, a4Hz);
    }
    return mono;
}

} // namespace

TEST_CASE("keycents: a C major progression at 440 Hz is C major with about zero cents")
{
    const KeyCentsPrediction p = estimateKeyCents(progression(false, 440.0));
    CHECK(p.tonic == 0);
    CHECK_FALSE(p.minor);
    CHECK(p.keyConfidence > 0.6);
    CHECK(p.keyMargin > 0.02);
    CHECK(std::abs(p.cents) < 3.0);
    CHECK(p.centsConfidence > 0.5);
    CHECK(keyName(p) == "C major");
    CHECK(keySignatureOf(p) == model::KeySignature{0, false});
}

TEST_CASE("keycents: an A minor progression tuned to 432 Hz is A minor, 32 cents flat")
{
    const KeyCentsPrediction p = estimateKeyCents(progression(true, 432.0));
    CHECK(p.tonic == 9);
    CHECK(p.minor);
    CHECK(std::abs(p.cents - (-31.8)) < 3.0);
    CHECK(keyName(p) == "A minor");
    CHECK(keySignatureOf(p) == model::KeySignature{0, true});
    const model::Prediction layer = toPrediction(p, "src");
    CHECK(layer.kind == model::PredictionKind::Key);
    REQUIRE(layer.items.size() == 1);
    CHECK(layer.items[0].text == "A minor");
}

TEST_CASE("keycents: key signatures take the shorter side of the circle")
{
    KeyCentsPrediction p;
    p.tonic = 10; // Bb major: two flats
    CHECK(keySignatureOf(p) == model::KeySignature{-2, false});
    p.tonic = 6; // Gb major: six flats (not F# with six sharps, the tie goes to the flat side)
    CHECK(keySignatureOf(p) == model::KeySignature{6, false});
    p.tonic = 2; // D minor: one flat
    p.minor = true;
    CHECK(keySignatureOf(p) == model::KeySignature{-1, true});
    CHECK(keyName(p) == "D minor");
}

TEST_CASE("keycents: a file goes through decode and resampling; a stop request cancels")
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "keycents";
    std::filesystem::create_directories(dir);
    const std::filesystem::path wav = dir / "cmajor.wav";
    const std::vector<float> mono = progression(false, 440.0);
    std::vector<float> stereo;
    stereo.reserve(mono.size() * 2);
    for (const float s : mono) {
        stereo.push_back(s);
        stereo.push_back(s);
    }
    media::AudioFileWriter writer;
    REQUIRE(writer.open(wav, rate, 2, media::AudioFileFormat::WavFloat));
    REQUIRE(writer.write(stereo));
    REQUIRE(writer.close());

    std::vector<double> progress;
    const std::stop_token never;
    const Result<KeyCentsPrediction> p = predictKeyCents(wav, never, [&progress](double v) {
        progress.push_back(v);
    });
    REQUIRE(p);
    CHECK(p->tonic == 0);
    CHECK_FALSE(p->minor);
    CHECK(progress.back() == 1.0);

    std::stop_source cancel;
    cancel.request_stop();
    const Result<KeyCentsPrediction> cancelled = predictKeyCents(wav, cancel.get_token(), [](double) { });
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
}
