// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/routinetimer.h"

#include "generated/settingkeys.h"
#include "ui/routine/routinesession.h"
#include "ui/shell/settings.h"

namespace cc::ui
{

namespace
{

constexpr int secondsPerMinute = 60;
constexpr int clockIntervalMs = 1000;
constexpr int defaultMinutes = 10;

} // namespace

RoutineTimerController::RoutineTimerController(Settings &settings, RoutineSession &session, QObject *parent)
    : QObject(parent)
    , m_settings(&settings)
    , m_session(&session)
{
    connect(&m_clock, &QTimer::timeout, this, [this] {
        advance(1);
    });
    connect(m_session, &RoutineSession::slotsChanged, this, &RoutineTimerController::followSession);
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key key) {
        if (key == setting::Key::PracticeTimerMinutes && m_timer.state() == routine::TimerState::Idle) {
            followSession();
        }
    });
    followSession();
}

int RoutineTimerController::moduleIndexOf(const std::string &moduleId)
{
    const int number = moduleId.size() == 2 ? (moduleId[1] - '0') : 0;
    return number >= 1 && number <= routine::RoutineTimer::moduleCount ? number - 1 : 0;
}

const routine::RoutineTimer &RoutineTimerController::timer() const
{
    return m_timer;
}

int RoutineTimerController::currentModule() const
{
    return m_timer.module();
}

int RoutineTimerController::lengthSeconds(int module) const
{
    const nlohmann::json minutes = m_settings->value(setting::Key::PracticeTimerMinutes);
    const auto index = static_cast<std::size_t>(module);
    const int value = minutes.is_array() && minutes.size() > index ? minutes[index].get<int>() : defaultMinutes;
    return value * secondsPerMinute;
}

QString RoutineTimerController::moduleLabel(int module) const
{
    const auto &modules = m_session->definition().modules;
    const auto index = static_cast<std::size_t>(module);
    if (index < modules.size()) {
        return QString::fromStdString(modules[index].id) + QLatin1Char(' ')
            + QString::fromStdString(modules[index].name);
    }
    return QStringLiteral("0%1").arg(module + 1);
}

QString RoutineTimerController::remainingText() const
{
    const int seconds = m_timer.remainingSeconds();
    return QStringLiteral("%1:%2")
        .arg(seconds / secondsPerMinute, 2, 10, QLatin1Char('0'))
        .arg(seconds % secondsPerMinute, 2, 10, QLatin1Char('0'));
}

/// A module change re-enters the timer; a mode or option change inside the same module leaves it alone.
void RoutineTimerController::followSession()
{
    const int module = moduleIndexOf(m_session->state().moduleId);
    const bool sameModule = module == m_timer.module();
    if (sameModule && m_timer.state() != routine::TimerState::Idle) {
        return; // a running, paused or finished segment survives option changes inside its module
    }
    if (sameModule && m_timer.lengthSeconds() == lengthSeconds(module)) {
        return;
    }
    m_clock.stop();
    m_timer.enterModule(module, lengthSeconds(module));
    emit changed();
}

void RoutineTimerController::toggle()
{
    m_timer.toggle();
    if (m_timer.state() == routine::TimerState::Running) {
        m_clock.start(clockIntervalMs);
    } else {
        m_clock.stop();
    }
    emit changed();
}

void RoutineTimerController::reset()
{
    m_clock.stop();
    m_timer.reset();
    emit changed();
}

void RoutineTimerController::advance(int seconds)
{
    const bool finished = m_timer.advance(seconds);
    if (finished) {
        m_clock.stop();
    }
    emit changed();
    if (finished) {
        emit segmentFinished(m_timer.module());
    }
}

} // namespace cc::ui
