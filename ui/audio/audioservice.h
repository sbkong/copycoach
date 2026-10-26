// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/backend.h"
#include "core/audio/catalog.h"
#include "core/audio/engine.h"
#include "core/audio/fluidsynthnode.h"
#include "core/audio/sequence.h"
#include "core/model/score.h"
#include "core/theory/constants.h"
#include "ui/shell/apppaths.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

// GUI-side owner of the audio engine (audio-engine.md section 4): opens the device, loads sounds, turns routine
// scores into note sequences, posts commands, and polls the snapshot / events on a timer. Falls back to a
// silent, timer-driven NullBackend when no device opens, so the UI keeps working (and the RDP session too).
namespace cc::ui
{

class Settings;

class AudioService : public QObject
{
    Q_OBJECT
public:
    AudioService(AppPaths paths, Settings &settings, QObject *parent = nullptr);
    ~AudioService() override;
    Q_DISABLE_COPY_MOVE(AudioService)

    [[nodiscard]] QString deviceDescription() const;
    [[nodiscard]] audio::EngineState state() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] std::int64_t lengthTicks() const;
    [[nodiscard]] const std::vector<QString> &startupMessages() const;

    /// Replaces the playing sequence with the score (one measure per routine slot); loop covers the whole score.
    void setScore(const model::Score &score);
    void play(bool countIn);
    void stop();
    void togglePlay(bool countIn);
    void playFromStart(bool countIn);
    void seekToSlot(int slot);
    void setTempo(double bpm);
    void setLoopEnabled(bool enabled);
    void setMetronomeEnabled(bool enabled);
    void setSlotMask(std::uint64_t audible);
    void setInstrument(int channel);
    void setTimeSignature(int count, int unit);
    /// A fretboard / keyboard click: note on now, note off after the click length.
    void clickNote(int midi);
    void panic();

signals:
    void stateChanged(cc::audio::EngineState state);
    void positionChanged(std::int64_t ticks);
    void slotChanged(int slot);
    void deviceChanged(const QString &description);

private:
    void openDevice();
    void loadSounds();
    void poll();
    void drainEvents();
    void handleEvent(const audio::Event &event);
    void applyLoop();

    AppPaths m_paths;
    Settings *m_settings;
    audio::FluidSynthNode m_synth;
    audio::Engine m_engine;
    std::unique_ptr<audio::IAudioBackend> m_backend;
    std::unique_ptr<audio::MetronomeSet> m_metronomeSet;
    std::unique_ptr<audio::NoteSequence> m_sequence;
    std::vector<std::unique_ptr<audio::NoteSequence>> m_retired; // freed once the engine reports Swapped
    QTimer m_pollTimer;
    QTimer m_pumpTimer; // only with the NullBackend fallback
    QTimer m_clickOff;
    int m_clickNote = -1;
    int m_channel = audio::guitarChannel;
    bool m_loop = true;
    double m_sampleRate = 48000.0;
    std::int64_t m_measureTicks = 4 * theory::ticksPerQuarter; // 4/4 until setTimeSignature
    QString m_deviceDescription;
    audio::EngineState m_lastState = audio::EngineState::Stopped;
    std::vector<QString> m_startupMessages;
};

} // namespace cc::ui
