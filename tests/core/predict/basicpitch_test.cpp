// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Note prediction with the bundled Basic Pitch model (copy-spec.md section 4 "음"): a synthesised guitar-like
// line comes back as those notes at those times; the decoder reproduces note_creation.py on plain activations.
#include "core/media/audiofile.h"
#include "core/model/scorebuilder.h"
#include "core/predict/basicpitch.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <set>
#include <vector>

using namespace cc;
using namespace cc::predict;

namespace
{

const std::filesystem::path s_model = std::filesystem::path(CC_DATA_DIR) / basicPitchModelFile;
constexpr int rate = basicPitchSampleRate;

/// Plucked-ish notes: four decaying harmonics, 10 ms attack.
void addNote(std::vector<float> &out, double startS, double lengthS, int midi)
{
    const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    const auto start = static_cast<std::size_t>(startS * rate);
    const auto n = static_cast<std::size_t>(lengthS * rate);
    for (std::size_t i = 0; i < n && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double env = std::min(1.0, t / 0.01) * std::exp(-2.0 * t);
        double v = 0.0;
        for (int h = 1; h <= 4; ++h) {
            v += std::sin(2.0 * std::numbers::pi * f0 * h * t) / h;
        }
        out[start + i] += static_cast<float>(0.2 * env * v);
    }
}

/// E3 G3 A3 B3 D4 E4 at half-second steps (a blues-ish line in the guitar's middle), 4 s in all.
std::vector<float> line()
{
    std::vector<float> mono(static_cast<std::size_t>(rate) * 4, 0.0F);
    const std::vector<int> midis{52, 55, 57, 59, 62, 64};
    for (std::size_t i = 0; i < midis.size(); ++i) {
        addNote(mono, 0.5 + (0.5 * static_cast<double>(i)), 0.45, midis[i]);
    }
    return mono;
}

} // namespace

TEST_CASE("basicpitch: a synthesised line comes back as its notes at their times")
{
    REQUIRE(std::filesystem::exists(s_model));
    std::vector<double> progress;
    const std::stop_token never;
    const Result<NoteActivations> act = runBasicPitch(line(), s_model, never, [&progress](double p) {
        progress.push_back(p);
    });
    REQUIRE(act);
    CHECK(act->frames > 300); // 4 s at 86 fps, minus the stitching
    CHECK(act->note.size() == act->frames * basicPitchBins);
    CHECK(progress.back() == 1.0);

    const std::vector<NoteEvent> notes = notesFromActivations(*act, BasicPitchOptions{});
    REQUIRE_FALSE(notes.empty());
    std::set<int> midis;
    for (const NoteEvent &note : notes) {
        midis.insert(note.midi);
        CHECK(note.endSeconds > note.startSeconds);
        CHECK(note.amplitude > 0.0);
    }
    for (const int expected : {52, 55, 57, 59, 62, 64}) {
        INFO("midi " << expected);
        CHECK(midis.contains(expected));
    }
    // the first note starts at 0.5 s, within a few frames
    const auto first = std::ranges::find(notes, 52, &NoteEvent::midi);
    REQUIRE(first != notes.end());
    CHECK(std::abs(first->startSeconds - 0.5) < 0.06);
}

TEST_CASE("basicpitch: the decoder reproduces output_to_notes_polyphonic on plain activations")
{
    NoteActivations act;
    act.frames = 100;
    act.note.assign(act.frames * basicPitchBins, 0.0F);
    act.onset.assign(act.frames * basicPitchBins, 0.0F);
    const std::size_t bin = 40; // midi 61
    for (std::size_t f = 10; f < 40; ++f) {
        act.note[(f * basicPitchBins) + bin] = 0.9F;
    }
    act.onset[(10 * basicPitchBins) + bin] = 0.8F; // a clear onset at frame 10
    for (std::size_t f = 60; f < 90; ++f) {
        act.note[(f * basicPitchBins) + bin + 7] = 0.7F; // energy without an onset: the melodia trick finds it
    }
    BasicPitchOptions options;
    options.inferOnsets = false;
    const std::vector<NoteEvent> notes = notesFromActivations(act, options);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].midi == 61);
    CHECK(std::abs(notes[0].startSeconds - (10.0 / basicPitchFps)) < 0.02);
    CHECK(std::abs(notes[0].endSeconds - (40.0 / basicPitchFps)) < 0.02);
    CHECK(std::abs(notes[0].amplitude - 0.9) < 1e-5);
    CHECK(notes[1].midi == 68);

    options.melodiaTrick = false;
    CHECK(notesFromActivations(act, options).size() == 1);
    options.minHz = 500.0; // above both notes: nothing survives
    CHECK(notesFromActivations(act, options).empty());
}

TEST_CASE("basicpitch: events become grid-snapped prediction items with a provisional string and fret")
{
    model::IdGenerator ids = model::IdGenerator::seeded(5);
    model::ScoreSpec spec;
    spec.tempoBpm = 120.0; // a quarter = 0.5 s = 960 ticks
    spec.measures.assign(2, model::MeasureSpec{{{model::Dur::Whole, {{1, 3}}}}});
    const model::Score score = model::buildScore(spec, ids);
    const model::TimeMap map = model::TimeMap::fromSyncPoints(score, {}, 1000.0);
    const std::vector<NoteEvent> notes{{0.49, 0.98, 52, 0.8}, {1.02, 1.26, 55, 0.7}, {2.0, 2.5, 20, 0.5}};
    const model::Prediction p = toPrediction(notes, "src", map, theory::Tuning::standardSix());
    CHECK(p.kind == model::PredictionKind::Notes);
    REQUIRE(p.items.size() == 3);
    CHECK(p.items[0].tick == 960);
    CHECK(p.items[0].dur == 960);
    CHECK(p.items[0].midi == 52);
    const theory::StringFret first = p.items[0].position.value_or(theory::StringFret{0, 0});
    CHECK(first.string == 5); // E3: fret 7 on the A string lies nearest to the starting fret 5
    CHECK(first.fret == 7);
    CHECK(p.items[1].tick == 1920);
    CHECK(p.items[1].dur == 480);
    CHECK_FALSE(p.items[2].position); // midi 20 is below the guitar
}

TEST_CASE("basicpitch: a file goes through decode and resampling; a stop request cancels")
{
    REQUIRE(std::filesystem::exists(s_model));
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "copycoach-tests" / "basicpitch";
    std::filesystem::create_directories(dir);
    const std::filesystem::path wav = dir / "line.wav";
    const std::vector<float> mono = line();
    media::AudioFileWriter writer;
    REQUIRE(writer.open(wav, rate, 1, media::AudioFileFormat::WavFloat));
    REQUIRE(writer.write(mono));
    REQUIRE(writer.close());
    const std::stop_token never;
    const Result<std::vector<NoteEvent>> notes = predictNotes(wav, s_model, BasicPitchOptions{}, never, [](double) { });
    REQUIRE(notes);
    CHECK(notes->size() >= 6);

    std::stop_source cancel;
    cancel.request_stop();
    const Result<std::vector<NoteEvent>> cancelled =
        predictNotes(wav, s_model, BasicPitchOptions{}, cancel.get_token(), [](double) { });
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == ErrorCode::Cancelled);
}
