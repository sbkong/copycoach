// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/routinesession.h"

#include "core/model/scorebuilder.h"

#include <algorithm>
#include <utility>

namespace cc::ui
{

namespace
{

std::uint32_t bit(int slot)
{
    return slot >= 0 && slot < RoutineSession::maxSlots ? 1U << static_cast<unsigned>(slot) : 0U;
}

} // namespace

RoutineSession::RoutineSession(routine::RoutineDefinition definition, routine::RoutineState state, QObject *parent)
    : QObject(parent)
    , m_definition(std::move(definition))
    , m_state(std::move(state))
    , m_tuning(theory::Tuning::standardSix())
{
    if (module() == nullptr && !m_definition.modules.empty()) {
        m_state.moduleId = m_definition.modules.front().id;
    }
    if (mode() == nullptr && module() != nullptr && !module()->modes.empty()) {
        m_state.modeId = module()->modes.front().id;
    }
    if (m_state.keys.empty()) {
        m_state.keys.push_back(m_state.currentKey);
    }
    applyModeDefaults();
    rebuild();
}

const routine::RoutineDefinition &RoutineSession::definition() const
{
    return m_definition;
}

const routine::RoutineState &RoutineSession::state() const
{
    return m_state;
}

const routine::ModuleDefinition *RoutineSession::module() const
{
    const auto it = std::ranges::find(m_definition.modules, m_state.moduleId, &routine::ModuleDefinition::id);
    return it == m_definition.modules.end() ? nullptr : &*it;
}

const routine::ModeDefinition *RoutineSession::mode() const
{
    const routine::ModuleDefinition *mod = module();
    if (mod == nullptr) {
        return nullptr;
    }
    const auto it = std::ranges::find(mod->modes, m_state.modeId, &routine::ModeDefinition::id);
    return it == mod->modes.end() ? nullptr : &*it;
}

const std::vector<Slot> &RoutineSession::slotList() const
{
    return m_slots;
}

const theory::Tuning &RoutineSession::tuning() const
{
    return m_tuning;
}

int RoutineSession::selectedSlot() const
{
    return m_selected;
}

int RoutineSession::playingSlot() const
{
    return m_playing;
}

bool RoutineSession::isMuted(int slot) const
{
    return (m_muteMask & bit(slot)) != 0U;
}

bool RoutineSession::isSoloed(int slot) const
{
    return (m_soloMask & bit(slot)) != 0U;
}

bool RoutineSession::hasSolo() const
{
    return m_soloMask != 0U;
}

std::uint64_t RoutineSession::audibleMask() const
{
    const std::uint32_t audible = hasSolo() ? m_soloMask : ~m_muteMask;
    return static_cast<std::uint64_t>(audible);
}

routine::PlaybackStyle RoutineSession::playbackStyle() const
{
    return m_style;
}

/// The positions that sound for a slot: the dots the root display option leaves on.
routine::Voicing RoutineSession::playedVoicing(const Slot &slot) const
{
    routine::Voicing voicing;
    for (const SlotNote &note : slot.notes) {
        if (noteShown(note, m_rootDisplay)) {
            voicing.push_back(note.position);
        }
    }
    return voicing;
}

model::Score RoutineSession::buildScore() const
{
    model::ScoreSpec spec;
    spec.timeSig = m_state.timeSig;
    spec.tempoBpm = m_state.tempoBpm;
    spec.tuning = m_tuning;
    for (const Slot &slot : m_slots) {
        const routine::Voicing voicing = playedVoicing(slot);
        if (slot.isScaleRun) {
            const auto measures = routine::scaleRunMeasures(voicing, m_state.timeSig);
            spec.measures.insert(spec.measures.end(), measures.begin(), measures.end());
        } else {
            const auto measures = routine::slotMeasures({voicing}, m_state.timeSig, m_style);
            spec.measures.insert(spec.measures.end(), measures.begin(), measures.end());
        }
    }
    model::IdGenerator ids;
    return model::buildScore(spec, ids);
}

/// Options the mode defines but the state lacks get the first listed value (first = default, routine.md 4).
void RoutineSession::applyModeDefaults()
{
    const routine::ModeDefinition *m = mode();
    if (m == nullptr) {
        return;
    }
    for (const auto &[name, values] : m->options) {
        if (!m_state.options.contains(name) && !values.empty()) {
            m_state.options[name] = values.front();
        }
    }
}

void RoutineSession::rebuild()
{
    m_slots = buildSlots(m_state, m_tuning);
    m_selected = m_slots.empty() ? -1 : std::clamp(m_selected, 0, static_cast<int>(m_slots.size()) - 1);
    emit slotsChanged();
    emit selectionChanged(m_selected);
}

void RoutineSession::setModule(const std::string &moduleId)
{
    if (moduleId == m_state.moduleId) {
        return;
    }
    m_state.moduleId = moduleId;
    m_state.modeId = module() != nullptr && !module()->modes.empty() ? module()->modes.front().id : std::string();
    m_state.options.clear();
    applyModeDefaults();
    rebuild();
}

void RoutineSession::setMode(const std::string &modeId)
{
    if (modeId == m_state.modeId) {
        return;
    }
    m_state.modeId = modeId;
    m_state.options.clear();
    applyModeDefaults();
    rebuild();
}

void RoutineSession::setOption(const std::string &name, const std::string &value)
{
    if (m_state.options[name] == value) {
        return;
    }
    m_state.options[name] = value;
    rebuild();
}

void RoutineSession::setCurrentKey(theory::PitchClass key)
{
    if (key == m_state.currentKey) {
        return;
    }
    m_state.currentKey = key;
    rebuild();
}

void RoutineSession::setKeys(const std::vector<theory::PitchClass> &keys)
{
    if (keys.empty()) {
        return;
    }
    m_state.keys = keys;
    if (std::ranges::find(keys, m_state.currentKey) == keys.end()) {
        setCurrentKey(keys.front());
    }
}

void RoutineSession::setTempo(double bpm)
{
    const double clamped = std::clamp(bpm, theory::tempoMinBpm, theory::tempoMaxBpm);
    if (qFuzzyCompare(clamped, m_state.tempoBpm)) {
        return;
    }
    m_state.tempoBpm = clamped;
    emit tempoChanged(clamped);
}

void RoutineSession::setPlaybackStyle(routine::PlaybackStyle style)
{
    if (style == m_style) {
        return;
    }
    m_style = style;
    emit playbackStyleChanged(style);
}

void RoutineSession::setRootDisplay(RootDisplay display)
{
    if (display == m_rootDisplay) {
        return;
    }
    m_rootDisplay = display;
    emit slotsChanged();
}

RootDisplay RoutineSession::rootDisplay() const
{
    return m_rootDisplay;
}

void RoutineSession::setSelectedSlot(int slot)
{
    if (slot < 0 || std::cmp_greater_equal(slot, m_slots.size()) || slot == m_selected) {
        return;
    }
    m_selected = slot;
    emit selectionChanged(slot);
}

void RoutineSession::setPlayingSlot(int slot)
{
    if (slot == m_playing) {
        return;
    }
    m_playing = slot;
    emit playingSlotChanged(slot);
}

void RoutineSession::toggleMute(int slot)
{
    m_muteMask ^= bit(slot);
    emit mixChanged();
}

void RoutineSession::toggleSolo(int slot)
{
    m_soloMask ^= bit(slot);
    emit mixChanged();
}

void RoutineSession::muteAll()
{
    m_muteMask = (1U << static_cast<unsigned>(m_slots.size())) - 1U;
    emit mixChanged();
}

void RoutineSession::clearSolo()
{
    m_soloMask = 0U;
    emit mixChanged();
}

} // namespace cc::ui
