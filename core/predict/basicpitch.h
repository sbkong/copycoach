// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/jobs/job.h"
#include "core/model/prediction.h"
#include "core/model/timemap.h"
#include "core/theory/tuning.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

// Note prediction with Spotify's Basic Pitch (ICASSP 2022 model, Apache-2.0, bundled as
// data/models/basic_pitch/nmp.onnx; copy-spec.md section 4 "음"): 22050 Hz mono in windows of 43844 samples
// with 30 overlapping frames, the model's note and onset activations (86 fps, 88 piano keys from A0) stitched
// the way basic_pitch.inference does, then its polyphonic note decoding (note_creation.py) reproduced here.
// Pitch bends (the contour output) are not used.
namespace cc::predict
{

constexpr int basicPitchSampleRate = 22050;
constexpr std::size_t basicPitchHop = 256;
constexpr std::size_t basicPitchWindowSamples = 43844; // 2 s minus one hop
constexpr std::size_t basicPitchFramesPerWindow = 172;
constexpr std::size_t basicPitchOverlapFrames = 30;
constexpr std::size_t basicPitchBins = 88;
constexpr int basicPitchMidiOffset = 21; // bin 0 = A0
constexpr double basicPitchFps = static_cast<double>(basicPitchSampleRate) / basicPitchHop;
constexpr const char *basicPitchModelFile = "models/basic_pitch/nmp.onnx"; // under the app data folder

/// basic_pitch.inference.predict defaults.
struct BasicPitchOptions {
    double onsetThreshold = 0.5;
    double frameThreshold = 0.3;
    int minNoteFrames = 11; // 127.7 ms
    std::optional<double> minHz; // nullopt = all 88 keys
    std::optional<double> maxHz;
    bool inferOnsets = true;
    bool melodiaTrick = true;
    int energyToleranceFrames = 11;
};

/// Frame-wise activations over the whole piece, row-major [frames][88].
struct NoteActivations {
    std::size_t frames = 0;
    std::vector<float> note;
    std::vector<float> onset;
};

struct NoteEvent {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midi = 0;
    double amplitude = 0.0; // mean note activation, 0..1
};

/// The model over the windows; Cancelled between windows.
[[nodiscard]] Result<NoteActivations> runBasicPitch(std::span<const float> mono22k,
                                                    const std::filesystem::path &model,
                                                    const std::stop_token &stop,
                                                    const jobs::Report &report);
/// note_creation.output_to_notes_polyphonic plus model_frames_to_time, in seconds, sorted by start.
[[nodiscard]] std::vector<NoteEvent> notesFromActivations(const NoteActivations &activations,
                                                          const BasicPitchOptions &options);
/// Decode, mix down, resample, model, decode notes.
[[nodiscard]] Result<std::vector<NoteEvent>> predictNotes(const std::filesystem::path &audio,
                                                          const std::filesystem::path &model,
                                                          const BasicPitchOptions &options,
                                                          const std::stop_token &stop,
                                                          const jobs::Report &report);

/// The prediction layer form: ticks through the time map, snapped to the 16th grid, lengths to the nearest
/// note value, and a provisional string/fret from the tuning (the nearest fret to the previous note; the
/// fingering engine replaces this later). Items the tuning cannot play are left without a position.
[[nodiscard]] model::Prediction toPrediction(const std::vector<NoteEvent> &notes,
                                             const std::string &sourceId,
                                             const model::TimeMap &map,
                                             const theory::Tuning &tuning);

} // namespace cc::predict
