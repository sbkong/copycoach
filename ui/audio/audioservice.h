// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/backend.h"
#include "core/audio/catalog.h"
#include "core/audio/engine.h"
#include "core/audio/fluidsynthnode.h"
#include "core/audio/renderworker.h"
#include "core/audio/sequence.h"
#include "core/model/score.h"
#include "core/model/timemap.h"
#include "core/theory/constants.h"
#include "generated/settingkeys.h"
#include "ui/shell/apppaths.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

// GUI-side owner of the audio engine (audio-engine.md section 4): opens the device, loads sounds, turns routine
// scores into note sequences, posts commands, and polls the snapshot / events on a timer. Falls back to a
// silent, timer-driven NullBackend when no device opens, so the UI keeps working (and the RDP session too).
// Device settings apply through reopenDevice(): the engine and synth are rebuilt at the new sample rate.
// Copy workspace (audio-engine.md section 15, audioservice_copy.cpp): the render worker streams the recording
// into the engine's chunk ring, the timing map turns its frames into ticks; the routine API is unchanged.
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
    [[nodiscard]] bool hasDevice() const;
    [[nodiscard]] std::optional<Error> deviceError() const;
    [[nodiscard]] std::optional<audio::StreamInfo> streamInfo() const;
    [[nodiscard]] audio::EngineState state() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] std::int64_t lengthTicks() const;
    [[nodiscard]] const std::vector<QString> &startupMessages() const;
    [[nodiscard]] const audio::SoundCatalog *catalog() const;

    /// Devices of every host API, from the live backend when it is PortAudio, else from a temporary one.
    [[nodiscard]] std::vector<audio::DeviceInfo> devices();
    /// Closes the stream, rebuilds the engine at the configured sample rate and reopens from the settings.
    [[nodiscard]] Result<void> reopenDevice();
    [[nodiscard]] Result<void> showAsioControlPanel(const QString &driverName, std::uintptr_t window);
    /// Reloads guitar / piano presets and the metronome set from the practice settings.
    void applySoundSettings();
    /// One second of the piano sound, for the "test tone" button.
    void testTone();

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

    // ---- copy workspace: file playback (audio-engine.md section 15). Added beside the routine API above.
    /// Routine (synth + metronome at the routine tempo) or copy (the loaded recording is the clock master).
    /// Stops playback; the recording is re-aimed at the current tick.
    void setCopyMode(bool on);
    [[nodiscard]] bool copyMode() const;
    /// One path = the original, several = stems mixed by the worker. Opened now, so the error is immediate.
    /// Replaces the previous set and resets the stem controls. Survives a device reopen.
    [[nodiscard]] Result<void> loadSources(std::vector<std::filesystem::path> paths);
    void unloadSources();
    [[nodiscard]] std::size_t sourceCount() const;
    [[nodiscard]] std::int64_t sourceLengthFrames() const; // device rate; 0 when nothing is loaded
    [[nodiscard]] std::int64_t sourceFrame() const; // the recording frame now playing; -1 before the first chunk
    /// Measure tempos of the score + the sync points of the loaded version -> the tick grid (a TimeMap at the
    /// device rate, rebuilt on reopen). Invalid points are dropped with a warning. The score's first time
    /// signature becomes the metronome / count-in meter.
    void setCopyTiming(const model::Score &score, std::vector<model::SyncPoint> points);
    [[nodiscard]] const model::TimeMap *copyTimeMap() const;
    /// Copy mode: moves the transport and the recording (through the map). Routine mode: transport only.
    void seekToTick(std::int64_t tick);
    /// Loop over a score range; the worker wraps on the source timeline. end <= start disables.
    void setCopyLoop(std::int64_t startTick, std::int64_t endTick);
    /// Speed factor clamped to RenderWorker::minSpeed..maxSpeed; the tick clock follows, so the shown BPM is the
    /// measure tempo times the factor (copy-spec.md section 3). Pitch in semitones (cents as the fraction), +-12.
    void setSpeed(double factor);
    void setPitch(double semitones);
    [[nodiscard]] double speed() const;
    [[nodiscard]] double pitch() const;
    /// Stem = index in the loaded set; out of range is ignored. Changes land once the ring drains (<= 250 ms).
    void setStemGain(std::size_t stem, float gain);
    void setStemMute(std::size_t stem, bool muted);
    void setStemSolo(std::size_t stem, bool solo);

signals:
    void stateChanged(cc::audio::EngineState state);
    void positionChanged(std::int64_t ticks);
    void slotChanged(int slot);
    void deviceChanged(const QString &description);
    void sourceFrameChanged(std::int64_t frame); // copy mode, right before positionChanged of the same poll
    void sourcesEnded(); // the recording ran out (no loop); the transport stopped

private:
    void buildEngine();
    void closeBackend();
    [[nodiscard]] audio::DeviceConfig configFromSettings() const;
    [[nodiscard]] Result<void> openPortAudio(const audio::DeviceConfig &config);
    void openNullFallback(audio::DeviceConfig config);
    void openDevice();
    void loadCatalog();
    void loadPreset(setting::Key key, int channel);
    void loadMetronomeSet();
    void resendState();
    void poll();
    void drainEvents();
    void handleEvent(const audio::Event &event);
    void applyLoop();
    void soundNote(int channel, int midi, int lengthMs);
    [[nodiscard]] bool post(const audio::Command &command);
    void releaseSwapped(const void *resource);

    // copy mode (audioservice_copy.cpp)
    void send(audio::CommandType type,
              std::int64_t a = 0,
              std::int64_t b = 0,
              double value = 0.0,
              const void *res = nullptr);
    void buildWorker();
    void resendCopyState();
    void resendStemControls();
    void startWorkerIfLoaded();
    void rebuildTimeMap();
    void postCopyTempo(std::int64_t tick);
    void applyCopyLoop();
    [[nodiscard]] std::int64_t frameForTick(std::int64_t tick) const;

    AppPaths m_paths;
    Settings *m_settings;
    double m_sampleRate = 48000.0;
    std::unique_ptr<audio::FluidSynthNode> m_synth;
    std::unique_ptr<audio::Engine> m_engine;
    std::unique_ptr<audio::IAudioBackend> m_backend;
    std::optional<audio::StreamInfo> m_streamInfo;
    std::optional<Error> m_deviceError;
    std::unique_ptr<audio::SoundCatalog> m_catalog;
    std::unique_ptr<audio::MetronomeSet> m_metronomeSet;
    std::unique_ptr<audio::MetronomeSet> m_retiredMetronomeSet;
    std::unique_ptr<audio::NoteSequence> m_sequence;
    std::vector<std::unique_ptr<audio::NoteSequence>> m_retired; // freed once the engine reports Swapped
    QTimer m_pollTimer;
    QTimer m_pumpTimer; // only with the NullBackend fallback
    QTimer m_clickOff;
    int m_clickNote = -1;
    int m_clickChannel = audio::guitarChannel;
    int m_channel = audio::guitarChannel;
    bool m_loop = true;
    bool m_metronome = false;
    double m_tempoBpm = theory::tempoMinBpm;
    std::uint64_t m_slotMask = ~std::uint64_t{0};
    std::int64_t m_measureTicks = 4 * theory::ticksPerQuarter; // 4/4 until setTimeSignature
    std::int64_t m_beatTicks = theory::ticksPerQuarter;
    int m_beatsPerMeasure = 4;
    QString m_deviceDescription;
    audio::EngineState m_lastState = audio::EngineState::Stopped;
    std::vector<QString> m_startupMessages;

    // copy mode. m_worker sits after m_engine on purpose: it holds the engine's ring, so it must die first.
    struct StemControl {
        float gain = 1.0F;
        bool muted = false;
        bool solo = false;
    };
    std::unique_ptr<audio::RenderWorker> m_worker;
    std::vector<std::filesystem::path> m_sourcePaths;
    std::vector<StemControl> m_stems;
    std::optional<model::Score> m_copyScore;
    std::vector<model::SyncPoint> m_syncPoints;
    std::unique_ptr<model::TimeMap> m_timeMap;
    std::vector<std::unique_ptr<model::TimeMap>> m_retiredMaps; // freed once the engine reports Swapped
    bool m_copyMode = false;
    double m_speed = 1.0;
    double m_pitch = 0.0;
    std::int64_t m_copyLoopStart = 0;
    std::int64_t m_copyLoopEnd = 0;
    std::int64_t m_lastTicks = 0; // last polled (or sought) tick: where a count-in or speed change starts from
    std::int64_t m_sourceFrame = -1;
};

} // namespace cc::ui
