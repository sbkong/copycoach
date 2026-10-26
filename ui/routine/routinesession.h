// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "core/routine/definition.h"
#include "core/routine/sequencer.h"
#include "core/routine/state.h"
#include "ui/routine/slotbuilder.h"

#include <QObject>

#include <cstdint>
#include <vector>

// The routine workspace's one model (components.md section 1): the definition, the user's state, the derived
// slotList, the single selection, the playing slot, and the mute / solo masks. Views subscribe; none keeps its own.
namespace cc::ui
{

class RoutineSession : public QObject
{
    Q_OBJECT
public:
    static constexpr int maxSlots = 8;

    RoutineSession(routine::RoutineDefinition definition, routine::RoutineState state, QObject *parent = nullptr);

    [[nodiscard]] const routine::RoutineDefinition &definition() const;
    [[nodiscard]] const routine::RoutineState &state() const;
    [[nodiscard]] const routine::ModuleDefinition *module() const;
    [[nodiscard]] const routine::ModeDefinition *mode() const;
    [[nodiscard]] const std::vector<Slot> &slotList() const;
    [[nodiscard]] const theory::Tuning &tuning() const;

    [[nodiscard]] int selectedSlot() const;
    [[nodiscard]] int playingSlot() const;
    [[nodiscard]] bool isMuted(int slot) const;
    [[nodiscard]] bool isSoloed(int slot) const;
    [[nodiscard]] bool hasSolo() const;
    /// Bit per slot that should sound (solo wins over mute), for the engine's SetSlotMask.
    [[nodiscard]] std::uint64_t audibleMask() const;
    [[nodiscard]] routine::PlaybackStyle playbackStyle() const;

    /// The playable score for the current slotList (one measure per slot, scale runs expanded).
    [[nodiscard]] model::Score buildScore() const;

    void setModule(const std::string &moduleId);
    void setMode(const std::string &modeId);
    void setOption(const std::string &name, const std::string &value);
    void setCurrentKey(theory::PitchClass key);
    void setKeys(const std::vector<theory::PitchClass> &keys);
    void setTempo(double bpm);
    void setPlaybackStyle(routine::PlaybackStyle style);
    void setSelectedSlot(int slot);
    void setPlayingSlot(int slot); // from the engine's SlotChanged, -1 when stopped
    void toggleMute(int slot);
    void toggleSolo(int slot);
    void muteAll();
    void clearSolo();

signals:
    void slotsChanged(); // module, mode, option or key changed: everything derived is new
    void selectionChanged(int slot);
    void playingSlotChanged(int slot);
    void mixChanged(); // mute / solo
    void tempoChanged(double bpm);
    void playbackStyleChanged(cc::routine::PlaybackStyle style);

private:
    void rebuild();
    void applyModeDefaults();

    routine::RoutineDefinition m_definition;
    routine::RoutineState m_state;
    theory::Tuning m_tuning;
    std::vector<Slot> m_slots;
    int m_selected = 0;
    int m_playing = -1;
    std::uint32_t m_muteMask = 0;
    std::uint32_t m_soloMask = 0;
    routine::PlaybackStyle m_style = routine::PlaybackStyle::Chord;
};

} // namespace cc::ui
