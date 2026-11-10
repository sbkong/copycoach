// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/audio/command.h"
#include "core/edit/commands.h"
#include "core/jobs/job.h"
#include "core/model/id.h"
#include "core/predict/basicpitch.h"
#include "core/predict/beatthis.h"
#include "core/predict/beatthismodel.h"
#include "core/predict/chordmini.h"
#include "core/predict/keycents.h"
#include "core/separation/stems.h"
#include "generated/actionids.h"
#include "generated/panelids.h"
#include "generated/settingkeys.h"
#include "generated/workspaceids.h"
#include "ui/shell/apppaths.h"

#include <QMainWindow>
#include <QUrl>

#include <filesystem>
#include <functional>
#include <span>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace cc::stats
{
class IStatsSink;
class IStatsReader;
} // namespace cc::stats

class QComboBox;
class QMenu;
class QDockWidget;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QToolBar;
class QToolButton;

// The application window (ia.md section 1): menu bar and toolbar built from the action registry, one dock per
// panel id whose content switches with the workspace, status bar, panel maximize / restore, and the layout
// saved per workspace. The shell is a workspace list (naming.md section 4): no routine / copy branches.
namespace cc::ui
{

class ActionRegistry;
class AudioService;
class CommandPalette;
class CopyBottomDock;
class CopyRecorder;
class CopyInspector;
class CopyStatsModel;
class VideoPanel;
class StemMixerPanel;
class Keymap;
class Icons;
class InlineMessage;
class InstrumentPanel;
class JobService;
class JobsChip;
class RoutineSession;
class ProjectSession;
class RoutineTimerController;
class ScoreEditor;
class ScoreView;
class Settings;
class StatsModel;
class Theme;
class TimelineView;
/// Everything the shell talks to; owned by the composition root (app/main.cpp), outlives the window.
struct ShellContext {
    const AppPaths *paths = nullptr;
    Theme *theme = nullptr;
    Settings *settings = nullptr;
    ActionRegistry *actions = nullptr;
    AudioService *audio = nullptr;
    RoutineSession *session = nullptr;
    Keymap *keymap = nullptr;
    stats::IStatsSink *stats = nullptr; // optional: no recorder without it (tests)
    const stats::IStatsReader *statsReader = nullptr; // optional: the stats dock shows its empty state without it
    JobService *jobs = nullptr; // optional: no jobs chip without it
};

struct Workspace {
    workspace::Id id;
    action::Id switchAction;
    QWidget *central = nullptr;
    std::map<panel::Id, QWidget *> panels; // content shown in that panel while the workspace is active
};

class Shell final : public QMainWindow
{
    Q_OBJECT
public:
    explicit Shell(const ShellContext &context, QWidget *parent = nullptr);
    ~Shell() override;
    Q_DISABLE_COPY_MOVE(Shell)

    void switchTo(workspace::Id id);
    [[nodiscard]] workspace::Id activeWorkspace() const;
    [[nodiscard]] bool isPanelMaximized() const;
    void showStatus(const QString &message);
    /// Opens the preferences dialog (modal) on a page; the status bar device chip opens the audio page.
    void showPreferences(int page);
    [[nodiscard]] RoutineTimerController *timer() const;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // shellmenus.cpp
    void buildMenus();
    void buildToolbar();
    void buildToolbarControls(QToolBar *bar);
    void buildStatusBar();
    void wireActions();
    void wireRoutineActions();
    void wireRoutineNavigation();
    void wireTempoAndKey();
    void wireTempo();
    void wireKey();
    void wireDockActions();
    void wireViewActions();
    void wireSettingActions();
    void wireTimerActions();
    void updateTimerChip();
    void updateTodayMinutes();
    void showJobResult(const jobs::JobEvent &event);
    void importFile(); // file.import: MusicXML or a recording, told apart by the extension
    void importScore(const std::filesystem::path &file);
    // shelldrop.cpp
    [[nodiscard]] static bool isImportable(const std::filesystem::path &path);
    void importPath(const std::filesystem::path &path);
    void importAudio(const std::filesystem::path &file);
    // shellvideo.cpp
    void wireVideoActions();
    void importVideo(const std::filesystem::path &file);
    void extractVideoAudio();
    void chooseVideoAudioTrack(int track);
    void updateVideoDock();
    // shellpredictcache.cpp (D-3): the grey layers of the active version survive a reopen
    void wirePredictionCache();
    void savePredictionCache(const model::Id &sourceId);
    void loadPredictionCache();
    // shellmisc.cpp: full screen, select all / go to, help, stats export, today's routine plan
    void wireMiscActions();
    void selectAllMeasures();
    void goToMeasure(int measure);
    void showAbout();
    void exportStats();
    void exportStatsTo(const std::filesystem::path &path); // without the file dialog (tests)
    void reassignToday();
    // shellstems.cpp
    void wireStemActions();
    [[nodiscard]] std::optional<separation::StemModel> stemModel() const;
    [[nodiscard]] std::optional<std::vector<std::filesystem::path>> cachedStems(const model::Id &sourceId) const;
    void runStemSeparation();
    void startStemSeparation(const separation::StemModel &model);
    void finishStemSeparation(separation::StemEngine engine, qint64 elapsedMs);
    void updateStemMixer();
    void applyAcceleratorSetting(); // separation.accelerator -> predict::setDefaultAccelerator
    [[nodiscard]] static QString providerLabel(); // "WebGPU" / "CPU" of the last session, for status text
    void wireCopyTransport(); // copy.speed.*, source changes
    void wireCopyActions(); // import, transport, editor, project
    void updateSpeedLabel();
    void pushCopyScore(); // the loaded score, its tempo and meter into the engine (synth playback)
    void pushRoutineScore();
    // shellproject.cpp
    void wireProjectActions();
    [[nodiscard]] bool confirmLoss(); // the loss dialog; false = the user cancelled
    [[nodiscard]] std::filesystem::path defaultProjectDir() const;
    void newProject();
    void openProject();
    void openProjectFolder(const std::filesystem::path &folder);
    void openRecent(const std::filesystem::path &folder);
    void markCheckable();
    void fillMenu(QMenu *menu, std::span<const action::Id> items);
    bool saveProject();
    bool saveProjectAs();
    void offerRecovery(const QString &savedAt);
    void updateProjectTitle();
    void updateAutosaveChip();
    void rebuildRecentMenu();
    // shellcopyinspector.cpp
    void wireCopyInspector();
    void showRange(const model::Id &id);
    void addRangeFromSelection();
    void removeChosenRange();
    void loopSelection(std::optional<model::Id> rangeId = std::nullopt); // what the recorder names
    void setStage(int stage); // copy.stage.{n} and the inspector buttons
    // shellpredict.cpp
    void wirePredictActions();
    // shellnotepredict.cpp
    void wireNotePredictActions();
    void runNotePrediction();
    void
    showNotePrediction(const std::vector<predict::NoteEvent> &notes, const model::Id &sourceId, bool fromCache = false);
    void acceptNotePrediction();
    void clearNotePrediction();
    [[nodiscard]] model::Prediction notePredictionItems() const; // ticks through the active version's map
    void hideAcceptedNotes(edit::MeasureSpan span);
    [[nodiscard]] theory::FingeringPattern fingeringPattern() const; // the override, else the setting
    void cycleFingeringPattern(int step); // fingering.candidate.next/prev: re-arranges the shown layer
    void acceptShownPrediction(); // copy.predict.accept: whichever layer was shown last
    // shellkeypredict.cpp
    void wireKeyPredictActions();
    void runKeyPrediction();
    void showKeyPrediction(const predict::KeyCentsPrediction &prediction);
    void acceptKeyPrediction();
    // shellchordpredict.cpp
    void wireChordPredictActions();
    void runChordPrediction();
    void startChordPrediction();
    [[nodiscard]] model::Prediction chordPredictionItems() const;
    void showChordPrediction(const std::vector<predict::ChordSegment> &segments,
                             const model::Id &sourceId,
                             bool fromCache = false);
    void acceptChordPrediction();
    void hideAcceptedChords(edit::MeasureSpan span);
    void clearChordPrediction();
    /// What an engine asks for on first use (states.md section 2): the inline offer, then the download job,
    /// then `then` (the prediction) once the files are in.
    struct ModelDownload {
        QString offer;
        QUrl baseUrl;
        std::filesystem::path dir;
        std::vector<predict::ModelFile> missing;
        std::function<void()> then;
    };
    void offerModelDownload(const ModelDownload &download);
    void startModelDownload(const ModelDownload &download);
    void runBeatPrediction();
    void startBeatPrediction();
    void
    showBeatPrediction(const predict::BeatPrediction &prediction, const model::Id &sourceId, bool fromCache = false);
    void acceptBeatPrediction();
    void clearBeatPrediction();
    void updatePredictActions();
    [[nodiscard]] predict::BeatThisVariant beatModelVariant() const;
    // shellcopydock.cpp
    void wireCopyDock();
    void refreshTrackTiming();
    void refreshWaveform();
    void addSyncPointAt(double seconds);
    [[nodiscard]] panel::Id bottomPanel() const; // the timeline (routine) or the audio track (copy)
    // shelleditor.cpp
    void wireEditorActions();
    void wireEditorToView();
    void updateEditorLabel();
    void showTransportState(audio::EngineState state);
    void markUnimplemented();
    // shell.cpp
    void build();
    void buildWorkspaces();
    void buildCopyWorkspace(); // the score, the project session and the copy docks
    void buildDocks();
    [[nodiscard]] QStackedWidget *stackFor(panel::Id id);
    void setDockVisible(panel::Id id, bool visible);
    void addDock(panel::Id id, QStackedWidget *stack);
    void placeDock(panel::Id id, QDockWidget *dock);
    void applyTheme();
    void applyAppearanceSettings(setting::Key key);
    void reloadThemeFromSettings();
    void maximizePanel();
    void restorePanel();
    void saveLayout();
    void restoreLayout(workspace::Id id);
    void syncDockActions();
    void resetLayout();
    [[nodiscard]] const Workspace &workspace(workspace::Id id) const;

    const AppPaths *m_paths;
    Theme *m_theme;
    Settings *m_settings;
    ActionRegistry *m_actions;
    AudioService *m_audio;
    RoutineSession *m_session;
    Keymap *m_keymap;
    stats::IStatsSink *m_stats;
    const stats::IStatsReader *m_statsReader;
    std::unique_ptr<Icons> m_icons;
    std::vector<Workspace> m_workspaces;
    workspace::Id m_active = workspace::Id::Routine;
    std::map<panel::Id, QDockWidget *> m_docks;
    std::map<panel::Id, QStackedWidget *> m_dockStacks;
    QStackedWidget *m_central = nullptr;
    InstrumentPanel *m_instrument = nullptr;
    RoutineTimerController *m_timer = nullptr;
    StatsModel *m_statsModel = nullptr;
    QToolButton *m_timerChip = nullptr;
    QLabel *m_todayLabel = nullptr;
    CommandPalette *m_palette = nullptr;
    TimelineView *m_timeline = nullptr;
    QSpinBox *m_tempo = nullptr;
    QComboBox *m_key = nullptr;
    QToolButton *m_deviceChip = nullptr;
    JobsChip *m_jobsChip = nullptr;
    ScoreView *m_scoreView = nullptr;
    ScoreEditor *m_editor = nullptr;
    ProjectSession *m_project = nullptr;
    CopyBottomDock *m_copyDock = nullptr;
    CopyInspector *m_copyInspector = nullptr;
    model::IdGenerator m_rangeIds;
    InlineMessage *m_copyMessage = nullptr;
    InlineMessage *m_shellMessage = nullptr; // above both workspaces: about, window-level notices
    CopyRecorder *m_copyRecorder = nullptr; // null without a stats sink
    VideoPanel *m_videoPanel = nullptr;
    StemMixerPanel *m_stemMixer = nullptr;
    bool m_separating = false;
    CopyStatsModel *m_copyStatsModel = nullptr;
    model::Id m_videoSource; // the version the panel has open
    std::optional<predict::BeatPrediction> m_beatPrediction;
    std::optional<predict::KeyCentsPrediction> m_keyPrediction;
    std::optional<std::vector<predict::NoteEvent>> m_noteEvents;
    model::Id m_notePredictionSource;
    std::optional<theory::FingeringPattern> m_fingeringOverride; // this project only, until the setting changes
    std::optional<std::vector<predict::ChordSegment>> m_chordSegments;
    model::Id m_chordPredictionSource;
    enum class ShownPrediction : std::uint8_t {
        None,
        Beats,
        Notes,
        Chords
    };
    ShownPrediction m_shownPrediction = ShownPrediction::None;
    model::Id m_beatPredictionSource; // the version it was computed from
    bool m_predicting = false;
    QLabel *m_autosaveLabel = nullptr;
    QMenu *m_recentMenu = nullptr;
    JobService *m_jobs = nullptr;
    QLabel *m_contextLabel = nullptr;
    QByteArray m_beforeMaximize;
    bool m_maximized = false;
};

} // namespace cc::ui
