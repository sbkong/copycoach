// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/audio/audioservice.h"

#include "core/audio/nullbackend.h"
#include "core/audio/portaudiobackend.h"
#include "core/model/duration.h"
#include "core/routine/sequencer.h"
#include "generated/settingkeys.h"
#include "ui/shell/errortext.h"
#include "ui/shell/settings.h"

#include <QLoggingCategory>

#include <utility>

namespace cc::ui
{

namespace
{

Q_LOGGING_CATEGORY(logAudio, "cc.audio")

using audio::Command;
using audio::CommandType;

constexpr int pollIntervalMs = 33; // ~30 fps for the playhead (audio-engine.md section 4)
constexpr int pumpIntervalMs = 16; // silent fallback: keep the transport moving at a plausible rate
constexpr int clickLengthMs = 200; // audio-engine.md section 8 item 5
constexpr float clickVelocity = 0.8F;
constexpr int sampleRateDefault = 48000;

Command command(CommandType type, std::int64_t a = 0, std::int64_t b = 0, double value = 0.0, const void *res = nullptr)
{
    return Command{type, a, b, value, res};
}

double sampleRateOf(const Settings &settings)
{
    const int configured = settings.intValue(setting::Key::AudioSampleRate);
    return configured > 0 ? configured : sampleRateDefault;
}

} // namespace

AudioService::AudioService(AppPaths paths, Settings &settings, QObject *parent)
    : QObject(parent)
    , m_paths(std::move(paths))
    , m_settings(&settings)
    , m_synth(sampleRateOf(settings))
    , m_engine(sampleRateOf(settings), m_synth)
    , m_sampleRate(sampleRateOf(settings))
{
    loadSounds();
    openDevice();
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioService::poll);
    m_pollTimer.start(pollIntervalMs);
    m_clickOff.setSingleShot(true);
    connect(&m_clickOff, &QTimer::timeout, this, [this] {
        if (m_clickNote >= 0) {
            static_cast<void>(m_engine.post(command(CommandType::NoteOff, m_channel, m_clickNote)));
            m_clickNote = -1;
        }
    });
}

AudioService::~AudioService()
{
    m_pollTimer.stop();
    m_pumpTimer.stop();
    if (m_backend) {
        static_cast<void>(m_backend->stop());
        m_backend->close();
    }
}

void AudioService::loadSounds()
{
    const auto catalog = audio::loadSoundCatalog(m_paths.dataDir / "sounds" / "catalog.json");
    if (!catalog) {
        m_startupMessages.push_back(errorText(catalog.error()));
        return;
    }
    const auto selectPreset = [this, &catalog](setting::Key key, int channel) {
        const audio::SoundEntry *entry = catalog->find(m_settings->stringValue(key).toStdString());
        if (entry == nullptr || !entry->preset) {
            return;
        }
        if (auto r = m_synth.loadSoundfont(m_paths.soundDir / entry->preset->soundfont); !r) {
            m_startupMessages.push_back(errorText(r.error()));
            return;
        }
        if (auto r = m_synth.selectPreset(channel, *entry->preset); !r) {
            m_startupMessages.push_back(errorText(r.error()));
        }
    };
    selectPreset(setting::Key::PracticeGuitarSound, audio::guitarChannel);
    selectPreset(setting::Key::PracticePianoSound, audio::pianoChannel);
    const audio::SoundEntry *metronome =
        catalog->find(m_settings->stringValue(setting::Key::PracticeMetronomeSound).toStdString());
    if (metronome != nullptr) {
        auto set = audio::loadMetronomeSet(m_paths.soundDir / metronome->dir, m_sampleRate);
        if (!set) {
            m_startupMessages.push_back(errorText(set.error()));
            return;
        }
        m_metronomeSet = std::make_unique<audio::MetronomeSet>(std::move(*set));
        static_cast<void>(m_engine.post(command(CommandType::SwapMetronomeSet, 0, 0, 0.0, m_metronomeSet.get())));
    }
}

void AudioService::openDevice()
{
    audio::DeviceConfig config;
    config.hostApi = m_settings->stringValue(setting::Key::AudioBackend).toStdString();
    config.outputDevice = m_settings->stringValue(setting::Key::AudioOutputDevice).toStdString();
    config.sampleRate = m_sampleRate;
    config.bufferFrames = m_settings->intValue(setting::Key::AudioBufferFrames);
    auto portaudio = std::make_unique<audio::PortAudioBackend>();
    const auto opened = portaudio->open(config, m_engine);
    if (opened && portaudio->start()) {
        const double latencyMs = opened->outputLatencySeconds * 1000.0;
        m_deviceDescription = tr("%1 · %2 kHz · 출력 지연 %3 ms")
                                  .arg(QString::fromStdString(config.hostApi))
                                  .arg(opened->sampleRate / 1000.0, 0, 'f', 1)
                                  .arg(latencyMs, 0, 'f', 1);
        m_backend = std::move(portaudio);
        return;
    }
    const Error error = opened ? Error{ErrorCode::DeviceOpenFailed, config.hostApi, "start failed"} : opened.error();
    qCWarning(logAudio) << "audio device unavailable:" << QString::fromStdString(error.detail);
    m_startupMessages.push_back(errorText(error));
    auto null = std::make_unique<audio::NullBackend>();
    config.bufferFrames = static_cast<int>(m_sampleRate * pumpIntervalMs / 1000.0);
    static_cast<void>(null->open(config, m_engine));
    static_cast<void>(null->start());
    auto *pump = null.get();
    connect(&m_pumpTimer, &QTimer::timeout, this, [pump, frames = config.bufferFrames] {
        pump->pump(frames);
    });
    m_pumpTimer.start(pumpIntervalMs);
    m_backend = std::move(null);
    m_deviceDescription = tr("오디오 장치 없음 (무음)");
}

QString AudioService::deviceDescription() const
{
    return m_deviceDescription;
}

audio::EngineState AudioService::state() const
{
    return m_lastState;
}

bool AudioService::isPlaying() const
{
    return m_lastState == audio::EngineState::Playing || m_lastState == audio::EngineState::CountIn;
}

std::int64_t AudioService::lengthTicks() const
{
    return m_sequence ? m_sequence->lengthTicks : 0;
}

const std::vector<QString> &AudioService::startupMessages() const
{
    return m_startupMessages;
}

void AudioService::setScore(const model::Score &score)
{
    auto sequence = std::make_unique<audio::NoteSequence>(audio::makeNoteSequence(routine::noteEvents(score), score));
    static_cast<void>(m_engine.post(command(CommandType::SwapSequence, 0, 0, 0.0, sequence.get())));
    if (m_sequence) {
        m_retired.push_back(std::move(m_sequence));
    }
    m_sequence = std::move(sequence);
    applyLoop();
}

void AudioService::applyLoop()
{
    const std::int64_t end = m_loop ? lengthTicks() : 0;
    static_cast<void>(m_engine.post(command(CommandType::SetLoop, 0, end)));
}

void AudioService::play(bool countIn)
{
    const int bars = countIn ? m_settings->intValue(setting::Key::PracticeCountInBars) : 0;
    static_cast<void>(m_engine.post(command(CommandType::Play, bars * m_measureTicks)));
}

void AudioService::stop()
{
    static_cast<void>(m_engine.post(command(CommandType::Stop)));
}

void AudioService::togglePlay(bool countIn)
{
    if (isPlaying()) {
        stop();
    } else {
        play(countIn);
    }
}

void AudioService::playFromStart(bool countIn)
{
    stop();
    static_cast<void>(m_engine.post(command(CommandType::Seek, 0)));
    play(countIn);
}

void AudioService::seekToSlot(int slot)
{
    if (!m_sequence || slot < 0 || std::cmp_greater_equal(slot, m_sequence->slotStartTicks.size())) {
        return;
    }
    static_cast<void>(
        m_engine.post(command(CommandType::Seek, m_sequence->slotStartTicks[static_cast<std::size_t>(slot)])));
}

void AudioService::setTempo(double bpm)
{
    static_cast<void>(m_engine.post(command(CommandType::SetTempo, 0, 0, bpm)));
}

void AudioService::setLoopEnabled(bool enabled)
{
    m_loop = enabled;
    applyLoop();
}

void AudioService::setMetronomeEnabled(bool enabled)
{
    static_cast<void>(m_engine.post(command(CommandType::SetMetronome, 0, enabled ? 1 : 0)));
}

void AudioService::setSlotMask(std::uint64_t audible)
{
    static_cast<void>(m_engine.post(command(CommandType::SetSlotMask, static_cast<std::int64_t>(audible))));
}

void AudioService::setInstrument(int channel)
{
    m_channel = channel;
    static_cast<void>(m_engine.post(command(CommandType::SetInstrument, channel)));
}

void AudioService::setTimeSignature(int count, int unit)
{
    const model::TimeSignature timeSig{count, unit};
    m_measureTicks = model::measureTicks(timeSig);
    static_cast<void>(
        m_engine.post(command(CommandType::SetTimeSignature, count, model::baseTicks(model::beatDur(timeSig)))));
}

void AudioService::clickNote(int midi)
{
    if (m_clickNote >= 0) {
        static_cast<void>(m_engine.post(command(CommandType::NoteOff, m_channel, m_clickNote)));
    }
    m_clickNote = midi;
    static_cast<void>(m_engine.post(command(CommandType::NoteOn, m_channel, midi, clickVelocity)));
    m_clickOff.start(clickLengthMs);
}

void AudioService::panic()
{
    stop();
    m_synth.allNotesOff();
}

void AudioService::handleEvent(const audio::Event &event)
{
    if (event.type == audio::EventType::SlotChanged) {
        emit slotChanged(static_cast<int>(event.a));
    } else if (event.type == audio::EventType::Swapped) {
        std::erase_if(m_retired, [&event](const auto &seq) {
            return seq.get() == event.resource;
        });
    }
}

void AudioService::drainEvents()
{
    audio::Event event;
    while (m_engine.pollEvent(event)) {
        handleEvent(event);
    }
}

void AudioService::poll()
{
    drainEvents();
    const audio::EngineSnapshot snapshot = m_engine.snapshot();
    if (snapshot.state != m_lastState) {
        m_lastState = snapshot.state;
        emit stateChanged(snapshot.state);
        if (snapshot.state == audio::EngineState::Stopped) {
            emit slotChanged(-1);
        }
    }
    emit positionChanged(snapshot.positionTicks);
}

} // namespace cc::ui
