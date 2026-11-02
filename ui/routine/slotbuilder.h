// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/routine/state.h"
#include "core/theory/tuning.h"
#include "generated/tokens.h"

#include <QString>

#include <cstdint>
#include <vector>

// Turns the routine state (module, mode, options, key) into the slotList the views show: chord strip chips,
// fretboard dots, timeline cells, and the voicings the sequencer plays. Pure functions over core/theory; the
// generation rules themselves live in docs/spec/routine-spec.md and core/theory.
namespace cc::ui
{

enum class DotKind : std::uint8_t {
    Shell, // circle (chord tone)
    Upper, // diamond (upper structure / tension)
    Scale, // circle, degree-coloured (module 01)
};

struct SlotNote {
    theory::StringFret position;
    QString degreeLabel; // "R", "3", "7", "9" for chords; mode degree "1".."7" for scales
    QString noteName; // flat spelling
    DotKind kind = DotKind::Shell;
    bool isRoot = false;
    theme::Token degreeColor = theme::Token::DegreeRootColor; // colour in degree mode
    bool altered = false; // a shell note outside the key (what a secondary dominant changed): accent outline
};

struct Slot {
    QString name; // "Cmaj7", "C 이오니안"
    QString roman; // "Imaj7", "" for scales
    QString rootString; // "6th" / "5th" / ""
    theme::Token chordColor = theme::Token::ChordCColor; // colour in chord mode (by scale degree)
    std::vector<SlotNote> notes;
    QString upperFour; // module 02 with the upper layer on: what the notes above the root spell (Cmaj9 = C + "Em7")
    bool isScaleRun = false; // played as a run (one note per beat up and down) instead of a chord
};

/// Slots for the state's module / mode / options in the state's current key. Unknown modules give no slotList.
std::vector<Slot> buildSlots(const routine::RoutineState &state, const theory::Tuning &tuning);

/// Chord-mode colour token for a scale degree 1..7 (components.md section 2.3: colour by degree, seven tokens).
theme::Token chordColorForDegree(int degree);

/// The root display option (view.fretboard-root). What it hides on the fretboard is also left out of playback
/// (routine-spec.md section 1 "playback = the dots that are on").
enum class RootDisplay : std::uint8_t {
    Show,
    Hide,
    Only,
};
RootDisplay rootDisplayOf(const QString &settingValue);
bool noteShown(const SlotNote &note, RootDisplay display);

} // namespace cc::ui
