// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/model/duration.h"
#include "core/theory/pitch.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// routine-state.json in the app data folder (routine.md section 4, routine-spec.md section 2.9): what the user last
// had selected. Not a project file; sounds and tempo live here for the routine workspace.
namespace cc::routine
{

struct RoutineState { // NOLINT(bugprone-exception-escape) plain aggregate; string members may allocate
    int schemaVersion = 1;
    std::string moduleId = "01";
    std::string modeId = "diagonal";
    std::map<std::string, std::string> options; // option name -> chosen value
    std::vector<theory::PitchClass> keys; // practise keys (multi-select), at least one
    theory::PitchClass currentKey = 0;
    double tempoBpm = 60.0;
    model::TimeSignature timeSig{4, 4};
    std::string metronomeSound = "click";
    std::string guitarSound = "guitar-jazz";
    std::string pianoSound = "piano-grand";
};

constexpr int routineStateSchemaVersion = 1;

Result<RoutineState> loadRoutineState(const std::filesystem::path &path);
Result<void> saveRoutineState(const std::filesystem::path &path, const RoutineState &state);

} // namespace cc::routine
