// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/audio/audioservice.h"

#include "core/audio/nullbackend.h"
#include "core/audio/portaudiobackend.h"
#include "core/model/duration.h"
#include "core/routine/sequencer.h"
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
constexpr int testToneMs = 1000;
constexpr int testToneNote = theory::a4;
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

audio::PortAudioBackend *asPortAudio(audio::IAudioBackend *backend)
{
    // The service owns two backend kinds; the PortAudio one is the only one with device queries.
    return dynamic_cast<audio::PortAudioBackend *>(backend); // NOLINT(cppcoreguidelines-pro-type-dynamic-cast)
}

} // namespace

AudioService::AudioService(AppPaths paths, Settings &settings, QObject *parent)
    : QObject(parent)
    , m_paths(std::move(paths))
    , m_settings(&settings)
{
    loadCatalog();
    buildEngine();
    openDevice();
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioService::poll);
    m_pollTimer.start(pollIntervalMs);
    m_clickOff.setSingleShot(true);
    connect(&m_clickOff, &QTimer::timeout, this, [this] {
        if (m_clickNote >= 0) {
            static_cast<void>(post(command(CommandType::NoteOff, m_clickChannel, m_clickNote)));
            m_clickNote = -1;
        }
    });
}

AudioService::~AudioService()
{
    m_pollTimer.stop();
    closeBackend();
}

bool AudioService::post(const audio::Command &command)
{
    return m_engine && m_engine->post(command);
}

// ---- engine and sounds -----------------------------------------------------------------------------------------

/// A fresh synth + engine at the configured sample rate, with the sounds loaded and the GUI state resent.
void AudioService::buildEngine()
{
    m_sampleRate = sampleRateOf(*m_settings);
    m_worker.reset(); // before the engine: it holds the engine's ring
    m_engine.reset();
    m_retiredMaps.clear(); // the old engine never reports them swapped
    m_synth = std::make_unique<audio::FluidSynthNode>(m_sampleRate);
    m_engine = std::make_unique<audio::Engine>(m_sampleRate, *m_synth);
    buildWorker();
    loadPreset(setting::Key::PracticeGuitarSound, audio::guitarChannel);
    loadPreset(setting::Key::PracticePianoSound, audio::pianoChannel);
    loadMetronomeSet();
    resendState();
}

void AudioService::loadCatalog()
{
    auto catalog = audio::loadSoundCatalog(m_paths.dataDir / "sounds" / "catalog.json");
    if (!catalog) {
        m_startupMessages.push_back(errorText(catalog.error()));
        return;
    }
    m_catalog = std::make_unique<audio::SoundCatalog>(std::move(*catalog));
}

void AudioService::loadPreset(setting::Key key, int channel)
{
    const audio::SoundEntry *entry = m_catalog ? m_catalog->find(m_settings->stringValue(key).toStdString()) : nullptr;
    if (entry == nullptr || !entry->preset) {
        return;
    }
    if (auto r = m_synth->loadSoundfont(m_paths.soundDir / entry->preset->soundfont); !r) {
        m_startupMessages.push_back(errorText(r.error()));
        return;
    }
    if (auto r = m_synth->selectPreset(channel, *entry->preset); !r) {
        m_startupMessages.push_back(errorText(r.error()));
    }
}

void AudioService::loadMetronomeSet()
{
    const audio::SoundEntry *entry = m_catalog
        ? m_catalog->find(m_settings->stringValue(setting::Key::PracticeMetronomeSound).toStdString())
        : nullptr;
    if (entry == nullptr) {
        return;
    }
    auto set = audio::loadMetronomeSet(m_paths.soundDir / entry->dir, m_sampleRate);
    if (!set) {
        m_startupMessages.push_back(errorText(set.error()));
        return;
    }
    m_retiredMetronomeSet = std::move(m_metronomeSet); // the engine may still read it until Swapped
    m_metronomeSet = std::make_unique<audio::MetronomeSet>(std::move(*set));
    static_cast<void>(post(command(CommandType::SwapMetronomeSet, 0, 0, 0.0, m_metronomeSet.get())));
}

void AudioService::applySoundSettings()
{
    loadPreset(setting::Key::PracticeGuitarSound, audio::guitarChannel);
    loadPreset(setting::Key::PracticePianoSound, audio::pianoChannel);
    loadMetronomeSet();
}

/// After a rebuild the new engine knows nothing: push tempo, loop, time signature, mix and sequence again.
void AudioService::resendState()
{
    static_cast<void>(post(command(CommandType::SetTimeSignature, m_beatsPerMeasure, m_beatTicks)));
    static_cast<void>(post(command(CommandType::SetTempo, 0, 0, m_tempoBpm)));
    static_cast<void>(post(command(CommandType::SetMetronome, 0, m_metronome ? 1 : 0)));
    static_cast<void>(post(command(CommandType::SetSlotMask, static_cast<std::int64_t>(m_slotMask))));
    static_cast<void>(post(command(CommandType::SetInstrument, m_channel)));
    if (m_sequence) {
        static_cast<void>(post(command(CommandType::SwapSequence, 0, 0, 0.0, m_sequence.get())));
    }
    resendCopyState();
    applyLoop();
}

// ---- device ------------------------------------------------------------------------------------------------------

audio::DeviceConfig AudioService::configFromSettings() const
{
    audio::DeviceConfig config;
    config.hostApi = m_settings->stringValue(setting::Key::AudioBackend).toStdString();
    config.inputDevice = m_settings->stringValue(setting::Key::AudioInputDevice).toStdString();
    config.outputDevice = m_settings->stringValue(setting::Key::AudioOutputDevice).toStdString();
    config.sampleRate = m_sampleRate;
    config.bufferFrames = m_settings->intValue(setting::Key::AudioBufferFrames);
    return config;
}

Result<void> AudioService::openPortAudio(const audio::DeviceConfig &config)
{
    auto portaudio = std::make_unique<audio::PortAudioBackend>();
    const auto opened = portaudio->open(config, *m_engine);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    if (const auto started = portaudio->start(); !started) {
        return started;
    }
    m_streamInfo = *opened;
    m_deviceError.reset();
    m_deviceDescription = tr("%1 · %2 kHz · 출력 지연 %3 ms")
                              .arg(QString::fromStdString(config.hostApi))
                              .arg(opened->sampleRate / 1000.0, 0, 'f', 1)
                              .arg(opened->outputLatencySeconds * 1000.0, 0, 'f', 1);
    m_backend = std::move(portaudio);
    return {};
}

void AudioService::openNullFallback(audio::DeviceConfig config)
{
    auto null = std::make_unique<audio::NullBackend>();
    config.bufferFrames = static_cast<int>(m_sampleRate * pumpIntervalMs / 1000.0);
    static_cast<void>(null->open(config, *m_engine));
    static_cast<void>(null->start());
    auto *pump = null.get();
    m_pumpTimer.disconnect();
    connect(&m_pumpTimer, &QTimer::timeout, this, [pump, frames = config.bufferFrames] {
        pump->pump(frames);
    });
    m_pumpTimer.start(pumpIntervalMs);
    m_backend = std::move(null);
    m_streamInfo.reset();
    m_deviceDescription = tr("오디오 장치 없음 (무음)");
}

void AudioService::openDevice()
{
    const audio::DeviceConfig config = configFromSettings();
    const auto opened = openPortAudio(config);
    if (opened) {
        return;
    }
    qCWarning(logAudio) << "audio device unavailable:" << QString::fromStdString(opened.error().detail);
    m_deviceError = opened.error();
    m_startupMessages.push_back(errorText(opened.error()));
    openNullFallback(config);
}

void AudioService::closeBackend()
{
    m_pumpTimer.stop();
    if (m_backend) {
        static_cast<void>(m_backend->stop());
        m_backend->close();
        m_backend.reset();
    }
}

Result<void> AudioService::reopenDevice()
{
    closeBackend();
    m_lastState = audio::EngineState::Stopped;
    buildEngine();
    const audio::DeviceConfig config = configFromSettings();
    const auto opened = openPortAudio(config);
    if (!opened) {
        m_deviceError = opened.error();
        openNullFallback(config);
    }
    emit deviceChanged(m_deviceDescription);
    emit stateChanged(m_lastState);
    return opened;
}

std::vector<audio::DeviceInfo> AudioService::devices()
{
    if (auto *live = asPortAudio(m_backend.get())) {
        return live->enumerate().value_or(std::vector<audio::DeviceInfo>{});
    }
    audio::PortAudioBackend probe;
    return probe.enumerate().value_or(std::vector<audio::DeviceInfo>{});
}

Result<void> AudioService::showAsioControlPanel(const QString &driverName, std::uintptr_t window)
{
    if (const auto *live = asPortAudio(m_backend.get())) {
        return live->showAsioControlPanel(driverName.toStdString(), window);
    }
    const audio::PortAudioBackend probe;
    return probe.showAsioControlPanel(driverName.toStdString(), window);
}

// ---- queries ------------------------------------------------------------------------------------------------------

QString AudioService::deviceDescription() const
{
    return m_deviceDescription;
}

bool AudioService::hasDevice() const
{
    return m_streamInfo.has_value();
}

std::optional<Error> AudioService::deviceError() const
{
    return m_deviceError;
}

std::optional<audio::StreamInfo> AudioService::streamInfo() const
{
    return m_streamInfo;
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

const audio::SoundCatalog *AudioService::catalog() const
{
    return m_catalog.get();
}

// ---- transport ---------------------------------------------------------------------------------------------------

void AudioService::setScore(const model::Score &score)
{
    auto sequence = std::make_unique<audio::NoteSequence>(audio::makeNoteSequence(routine::noteEvents(score), score));
    static_cast<void>(post(command(CommandType::SwapSequence, 0, 0, 0.0, sequence.get())));
    if (m_sequence) {
        m_retired.push_back(std::move(m_sequence));
    }
    m_sequence = std::move(sequence);
    applyLoop();
}

void AudioService::applyLoop()
{
    if (m_copyMode) {
        applyCopyLoop(); // the worker wraps on the source timeline (audio-engine.md section 15.3)
        return;
    }
    const std::int64_t end = m_loop ? lengthTicks() : 0;
    static_cast<void>(post(command(CommandType::SetLoop, 0, end)));
}

void AudioService::play(bool countIn)
{
    const int bars = countIn ? m_settings->intValue(setting::Key::PracticeCountInBars) : 0;
    static_cast<void>(post(command(CommandType::Play, bars * m_measureTicks)));
}

void AudioService::stop()
{
    static_cast<void>(post(command(CommandType::Stop)));
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
    seekToTick(0);
    play(countIn);
}

void AudioService::seekToSlot(int slot)
{
    if (!m_sequence || slot < 0 || std::cmp_greater_equal(slot, m_sequence->slotStartTicks.size())) {
        return;
    }
    static_cast<void>(post(command(CommandType::Seek, m_sequence->slotStartTicks[static_cast<std::size_t>(slot)])));
}

void AudioService::setTempo(double bpm)
{
    m_tempoBpm = bpm;
    static_cast<void>(post(command(CommandType::SetTempo, 0, 0, bpm)));
}

void AudioService::setLoopEnabled(bool enabled)
{
    m_loop = enabled;
    applyLoop();
}

void AudioService::setMetronomeEnabled(bool enabled)
{
    m_metronome = enabled;
    static_cast<void>(post(command(CommandType::SetMetronome, 0, enabled ? 1 : 0)));
}

void AudioService::setSlotMask(std::uint64_t audible)
{
    m_slotMask = audible;
    static_cast<void>(post(command(CommandType::SetSlotMask, static_cast<std::int64_t>(audible))));
}

void AudioService::setInstrument(int channel)
{
    m_channel = channel;
    static_cast<void>(post(command(CommandType::SetInstrument, channel)));
}

void AudioService::setTimeSignature(int count, int unit)
{
    const model::TimeSignature timeSig{count, unit};
    m_measureTicks = model::measureTicks(timeSig);
    m_beatTicks = model::baseTicks(model::beatDur(timeSig));
    m_beatsPerMeasure = count;
    static_cast<void>(post(command(CommandType::SetTimeSignature, count, m_beatTicks)));
}

void AudioService::soundNote(int channel, int midi, int lengthMs)
{
    if (m_clickNote >= 0) {
        static_cast<void>(post(command(CommandType::NoteOff, m_clickChannel, m_clickNote)));
    }
    m_clickNote = midi;
    m_clickChannel = channel;
    static_cast<void>(post(command(CommandType::NoteOn, channel, midi, clickVelocity)));
    m_clickOff.start(lengthMs);
}

void AudioService::clickNote(int midi)
{
    soundNote(m_channel, midi, clickLengthMs);
}

void AudioService::testTone()
{
    soundNote(audio::pianoChannel, testToneNote, testToneMs);
}

void AudioService::panic()
{
    stop();
    if (m_synth) {
        m_synth->allNotesOff();
    }
}

// ---- polling -----------------------------------------------------------------------------------------------------

void AudioService::handleEvent(const audio::Event &event)
{
    if (event.type == audio::EventType::SlotChanged) {
        emit slotChanged(static_cast<int>(event.a));
    } else if (event.type == audio::EventType::FileEnded) {
        emit sourcesEnded();
    } else if (event.type == audio::EventType::Swapped) {
        releaseSwapped(event.resource);
    }
}

void AudioService::releaseSwapped(const void *resource)
{
    std::erase_if(m_retired, [resource](const auto &seq) {
        return seq.get() == resource;
    });
    std::erase_if(m_retiredMaps, [resource](const auto &map) {
        return map.get() == resource;
    });
    if (m_retiredMetronomeSet && m_retiredMetronomeSet.get() == resource) {
        m_retiredMetronomeSet.reset();
    }
}

void AudioService::drainEvents()
{
    audio::Event event;
    while (m_engine && m_engine->pollEvent(event)) {
        handleEvent(event);
    }
}

void AudioService::poll()
{
    if (!m_engine) {
        return;
    }
    drainEvents();
    const audio::EngineSnapshot snapshot = m_engine->snapshot();
    if (snapshot.state != m_lastState) {
        m_lastState = snapshot.state;
        emit stateChanged(snapshot.state);
        if (snapshot.state == audio::EngineState::Stopped) {
            emit slotChanged(-1);
        }
    }
    m_lastTicks = snapshot.positionTicks;
    if (m_copyMode) {
        m_sourceFrame = snapshot.sourceFrame;
        emit sourceFrameChanged(snapshot.sourceFrame);
    }
    emit positionChanged(snapshot.positionTicks);
}

} // namespace cc::ui
