// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/routinerecorder.h"

#include "core/theory/pitch.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"

namespace cc::ui
{

namespace
{

constexpr int secondsPerMinute = 60;

} // namespace

RoutineRecorder::RoutineRecorder(stats::IStatsSink &sink,
                                 const RoutineTimerController &timer,
                                 const RoutineSession &session,
                                 QObject *parent)
    : QObject(parent)
    , m_sink(&sink)
    , m_timer(&timer)
    , m_session(&session)
{
    connect(m_timer, &RoutineTimerController::changed, this, &RoutineRecorder::onTimerChanged);
}

void RoutineRecorder::onTimerChanged()
{
    const routine::RoutineTimer &timer = m_timer->timer();
    const bool running = timer.state() == routine::TimerState::Running;
    if (running && !m_running) {
        recordStart(timer.module());
    } else if (!running && m_running) {
        recordStop();
    } else if (running && elapsedSeconds() / secondsPerMinute > m_minutesReported) {
        recordTick();
    }
    m_running = running;
}

std::string RoutineRecorder::moduleId(int module) const
{
    const auto &modules = m_session->definition().modules;
    const auto index = static_cast<std::size_t>(module);
    return index < modules.size() ? modules[index].id : "0" + std::to_string(module + 1);
}

int RoutineRecorder::elapsedSeconds() const
{
    return m_timer->timer().elapsedSeconds(m_module);
}

void RoutineRecorder::recordStart(int module)
{
    m_module = module;
    m_mode = m_session->state().modeId;
    m_minutesReported = elapsedSeconds() / secondsPerMinute;
    nlohmann::json keys = nlohmann::json::array();
    for (const theory::PitchClass key : m_session->state().keys) {
        keys.push_back(std::string(theory::flatName(key)));
    }
    m_sink->record(
        {std::string(stats::event::routineModuleStart),
         {{"module", moduleId(module)}, {"mode", m_mode}, {"keys", keys}, {"tempo", m_session->state().tempoBpm}}});
}

void RoutineRecorder::recordStop()
{
    m_sink->record({std::string(stats::event::routineModuleStop),
                    {{"module", moduleId(m_module)}, {"mode", m_mode}, {"elapsed", elapsedSeconds()}}});
}

void RoutineRecorder::recordTick()
{
    m_minutesReported = elapsedSeconds() / secondsPerMinute;
    m_sink->record(
        {std::string(stats::event::routineTimerTick), {{"module", moduleId(m_module)}, {"elapsed", elapsedSeconds()}}});
}

} // namespace cc::ui
