// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Shell: menu bar, toolbar, status bar and the action wiring (ia.md section 1 menu contents).
#include "ui/shell/shell.h"

#include "core/project/project.h"
#include "core/score/musicxmlimport.h"
#include "core/theory/pitch.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/projectsession.h"
#include "ui/jobs/jobservice.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/routine/instrumentpanel.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/routine/statsmodel.h"
#include "ui/routine/timelineview.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/commandpalette.h"
#include "ui/shell/errortext.h"
#include "ui/shell/jobschip.h"
#include "ui/shell/settings.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include <array>
#include <span>

namespace cc::ui
{

namespace
{

using action::Id;

constexpr Id separator = Id::Count;

// Menu contents (ia.md section 1). Template ids expand to all their instances.
constexpr std::array fileMenu{Id::FileNew,
                              Id::FileOpen,
                              Id::FileImport,
                              separator,
                              Id::FileSave,
                              Id::FileSaveAs,
                              separator,
                              Id::FileExportScore,
                              Id::FilePrint,
                              Id::FileExportStats};
constexpr std::array editMenu{Id::EditUndo,
                              Id::EditRedo,
                              Id::EditCut,
                              Id::EditCopy,
                              Id::EditPaste,
                              Id::EditSelectAll,
                              separator,
                              Id::EditGoTo};
constexpr std::array viewMenu{Id::WorkspaceRoutine,
                              Id::WorkspaceCopy,
                              separator,
                              Id::ViewDockNavigator,
                              Id::ViewDockInspector,
                              Id::ViewDockBottom,
                              Id::ViewDockInstrument,
                              Id::ViewDockStats,
                              Id::ViewStatsFull,
                              separator,
                              Id::InstrumentKeyboardStrip,
                              Id::FretboardLabel,
                              Id::FretboardColor,
                              separator,
                              Id::ViewPanelMaximize,
                              Id::ViewFullscreen,
                              Id::ViewRestore,
                              separator,
                              Id::ViewThemeDark,
                              Id::ViewThemeLight,
                              Id::ViewThemeSystem,
                              separator,
                              Id::AppCommandPalette};
constexpr std::array playMenu{Id::TransportPlayPause,
                              Id::TransportPlayFromStart,
                              Id::TransportLoop,
                              Id::TransportCountIn,
                              Id::TransportMetronome,
                              Id::TransportRecord,
                              separator,
                              Id::TransportPanic};
constexpr std::array routineMenu{Id::RoutineModuleN,
                                 separator,
                                 Id::RoutineTodayReassign,
                                 separator,
                                 Id::RoutineTimerToggle,
                                 Id::RoutineTimerReset};
constexpr std::array copyMenu{Id::CopyPhaseSetup,
                              Id::CopyPhaseLoop,
                              Id::CopyPhaseFinish,
                              separator,
                              Id::CopyStemSeparate,
                              Id::CopySectionAdd,
                              Id::CopySectionRemove,
                              Id::CopySyncPointAdd,
                              Id::CopyAbCycle};
constexpr std::array settingsMenu{Id::AppPreferences, Id::AppKeymapEditor, separator, Id::ViewResetLayout};
constexpr std::array helpMenu{Id::HelpManual, Id::HelpShortcuts, separator, Id::HelpAbout};

struct MenuSpec {
    const char *title = nullptr;
    std::span<const Id> items;
};

constexpr std::array<MenuSpec, 8> menus{{
    {"파일(&F)", fileMenu},
    {"편집(&E)", editMenu},
    {"보기(&V)", viewMenu},
    {"재생(&P)", playMenu},
    {"루틴(&R)", routineMenu},
    {"카피(&C)", copyMenu},
    {"설정(&S)", settingsMenu},
    {"도움말(&H)", helpMenu},
}};

constexpr std::array checkableActions{Id::TransportLoop,
                                      Id::TransportCountIn,
                                      Id::TransportMetronome,
                                      Id::ViewPanelMaximize,
                                      Id::ViewDockNavigator,
                                      Id::ViewDockInspector,
                                      Id::ViewDockBottom,
                                      Id::ViewDockStats,
                                      Id::WorkspaceRoutine,
                                      Id::WorkspaceCopy};

constexpr std::array toolbarTransport{Id::TransportPlayFromStart,
                                      Id::TransportPlayPause,
                                      Id::TransportLoop,
                                      Id::TransportCountIn,
                                      Id::TransportMetronome,
                                      Id::TransportRecord};

constexpr std::array<std::pair<Id, const char *>, 10> toolbarIcons{{
    {Id::TransportPlayFromStart, "skip-back"},
    {Id::TransportPlayPause, "play"},
    {Id::TransportLoop, "repeat"},
    {Id::TransportCountIn, "list-ordered"},
    {Id::TransportMetronome, "metronome"},
    {Id::TransportRecord, "mic"},
    {Id::AppCommandPalette, "command"},
    {Id::ViewPanelMaximize, "maximize-2"},
    {Id::InstrumentKeyboardStrip, "piano"},
    {Id::RoutineTimerToggle, "timer"},
}};

/// Actions that have no behaviour yet: disabled with the reason in the tooltip (states.md section 6).
constexpr std::array notYetImplemented{

    Id::FileExportScore,
    Id::FilePrint,
    Id::FileExportStats,

    Id::EditCut,
    Id::EditCopy,
    Id::EditPaste,
    Id::EditSelectAll,
    Id::EditGoTo,
    Id::ViewStatsFull,
    Id::ViewFullscreen,
    Id::TransportRecord,
    Id::RoutineTodayReassign,
    Id::HelpManual,
    Id::HelpShortcuts,
    Id::HelpAbout,
};

constexpr std::array<std::pair<Id, const char *>, 9> toolbarShortLabels{{
    {Id::TransportPlayFromStart, "처음부터"},
    {Id::TransportPlayPause, "재생"},
    {Id::TransportLoop, "루프"},
    {Id::TransportCountIn, "카운트인"},
    {Id::TransportMetronome, "메트로놈"},
    {Id::TransportRecord, "녹음"},
    {Id::AppCommandPalette, "팔레트"},
    {Id::ViewPanelMaximize, "패널 최대화"},
    {Id::WorkspaceRoutine, "루틴"},
}};

constexpr std::array<int, 12> circleOfFourths{0, 5, 10, 3, 8, 1, 6, 11, 4, 9, 2, 7};

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

int wrapIndex(int index, int count)
{
    return ((index % count) + count) % count;
}

void addMenuEntry(QMenu *menu, const ActionRegistry &actions, Id id)
{
    if (id == separator) {
        menu->addSeparator();
        return;
    }
    const action::Info &info = action::info(id);
    if (info.rangeMax == 0) {
        menu->addAction(actions.action(id));
        return;
    }
    for (int n = info.rangeMin; n <= info.rangeMax; ++n) {
        menu->addAction(actions.action(id, n));
    }
}

} // namespace

void Shell::markCheckable()
{
    for (const Id id : checkableActions) {
        m_actions->action(id)->setCheckable(true);
    }
    for (int n = 1; n <= 4; ++n) {
        m_actions->action(Id::RoutineModuleN, n)->setCheckable(true);
    }
    auto *themes = new QActionGroup(this);
    for (const Id id : {Id::ViewThemeDark, Id::ViewThemeLight, Id::ViewThemeSystem}) {
        m_actions->action(id)->setCheckable(true);
        themes->addAction(m_actions->action(id));
    }
}

void Shell::buildMenus()
{
    markCheckable();
    for (const MenuSpec &spec : menus) {
        fillMenu(menuBar()->addMenu(tr(spec.title)), spec.items);
    }
}

void Shell::fillMenu(QMenu *menu, std::span<const Id> items)
{
    for (const Id id : items) {
        addMenuEntry(menu, *m_actions, id);
        if (id == Id::FileOpen) {
            m_recentMenu = menu->addMenu(tr("최근")); // entries come from the project session
        }
    }
}

void Shell::buildToolbar()
{
    auto *bar = addToolBar(fromView(panel::info(panel::Id::ToolbarMain).label));
    bar->setObjectName(fromView(panel::idOf(panel::Id::ToolbarMain)));
    bar->setMovable(false);
    bar->setIconSize(QSize(m_theme->spacing(theme::Spacing::Icon), m_theme->spacing(theme::Spacing::Icon)));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (const auto &[id, icon] : toolbarIcons) {
        m_actions->action(id)->setIcon(m_icons->icon(QString::fromLatin1(icon)));
    }
    for (const auto &[id, text] : toolbarShortLabels) {
        m_actions->action(id)->setIconText(tr(text)); // toolbar shows the short form, menus keep the full label
    }
    for (const Workspace &ws : m_workspaces) {
        bar->addAction(m_actions->action(ws.switchAction));
    }
    bar->addSeparator();
    for (const Id id : toolbarTransport) {
        bar->addAction(m_actions->action(id));
    }
    bar->addSeparator();
    buildToolbarControls(bar);
    m_timerChip = new QToolButton(bar);
    m_timerChip->setDefaultAction(m_actions->action(Id::RoutineTimerToggle));
    m_timerChip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_timerChip->setFont(m_theme->font(theme::TypeStyle::Caption, true));
    bar->addWidget(m_timerChip);
    auto *spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    bar->addAction(m_actions->action(Id::ViewPanelMaximize));
    bar->addAction(m_actions->action(Id::AppCommandPalette));
}

void Shell::buildToolbarControls(QToolBar *bar)
{
    m_tempo = new QSpinBox(bar);
    m_tempo->setRange(static_cast<int>(theory::tempoMinBpm), static_cast<int>(theory::tempoMaxBpm));
    m_tempo->setPrefix(QStringLiteral("♩ "));
    m_tempo->setFont(m_theme->font(theme::TypeStyle::Body, true));
    m_tempo->setAccessibleName(tr("템포 BPM"));
    m_tempo->setValue(static_cast<int>(m_session->state().tempoBpm));
    bar->addWidget(m_tempo);
    m_key = new QComboBox(bar);
    for (const int key : circleOfFourths) {
        m_key->addItem(tr("키 %1").arg(fromView(theory::flatName(key))), key);
    }
    m_key->setAccessibleName(tr("현재 키"));
    bar->addWidget(m_key);
}

void Shell::buildStatusBar()
{
    m_deviceChip = new QToolButton(this);
    m_deviceChip->setText(m_audio->deviceDescription());
    m_deviceChip->setToolTip(tr("클릭 → 환경설정 › 오디오 장치"));
    m_deviceChip->setAccessibleName(tr("오디오 장치 (환경설정 열기)"));
    m_deviceChip->setFont(m_theme->font(theme::TypeStyle::Caption));
    m_deviceChip->setAutoRaise(true);
    connect(m_deviceChip, &QToolButton::clicked, this, [this] {
        showPreferences(static_cast<int>(PreferencesPage::AudioDevice));
    });
    statusBar()->addWidget(m_deviceChip);
    m_todayLabel = new QLabel(this);
    m_todayLabel->setFont(m_theme->font(theme::TypeStyle::Caption, true));
    m_todayLabel->setAccessibleName(tr("오늘 연습 시간"));
    statusBar()->addWidget(m_todayLabel);
    connect(m_statsModel, &StatsModel::changed, this, &Shell::updateTodayMinutes);
    updateTodayMinutes();
    if (m_jobs != nullptr) {
        m_jobsChip = new JobsChip(*m_theme, *m_jobs, this);
        statusBar()->addWidget(m_jobsChip);
        connect(m_jobs, &JobService::finished, this, &Shell::showJobResult);
    }
    m_contextLabel = new QLabel(this);
    m_contextLabel->setFont(m_theme->font(theme::TypeStyle::Caption, true));
    statusBar()->addPermanentWidget(m_contextLabel);
    m_autosaveLabel = new QLabel(this);
    m_autosaveLabel->setFont(m_theme->font(theme::TypeStyle::Caption, true));
    m_autosaveLabel->setAccessibleName(tr("자동 저장 시각"));
    statusBar()->addPermanentWidget(m_autosaveLabel);
    connect(m_audio, &AudioService::deviceChanged, m_deviceChip, &QToolButton::setText);
    for (const QString &message : m_audio->startupMessages()) {
        statusBar()->showMessage(message);
    }
}

/// "오늘 N분" (stats.md section 3): 0 rather than hidden when nothing is recorded, so the slot never jumps.
void Shell::updateTodayMinutes()
{
    m_todayLabel->setText(tr("오늘 %1분").arg(m_statsModel->todayMinutes()));
}

/// "스템 분리 완료 · 18 s" is a status, not a record (states.md section 2); a cancel says so in one word.
void Shell::showJobResult(const jobs::JobEvent &event)
{
    const QString name = JobService::kindLabel(event.kind);
    if (!event.error) {
        showStatus(tr("%1 완료 · %2 s").arg(name).arg(event.progress.elapsed.count() / 1000));
    } else if (event.error->code == ErrorCode::Cancelled) {
        showStatus(tr("%1 취소됨").arg(name));
    } else {
        showStatus(tr("%1: %2").arg(name, errorText(*event.error)));
    }
}

void Shell::markUnimplemented()
{
    for (const Id id : notYetImplemented) {
        QAction *action = m_actions->action(id);
        action->setEnabled(false);
        action->setToolTip(tr("다음 조각에서 구현됩니다"));
    }
}

void Shell::wireActions()
{
    const auto countIn = [this] {
        return m_actions->action(Id::TransportCountIn)->isChecked();
    };
    connect(m_actions->action(Id::TransportPlayPause), &QAction::triggered, this, [this, countIn] {
        m_audio->togglePlay(countIn());
    });
    connect(m_actions->action(Id::TransportPlayFromStart), &QAction::triggered, this, [this, countIn] {
        m_audio->playFromStart(countIn());
    });
    connect(m_actions->action(Id::TransportLoop), &QAction::toggled, m_audio, &AudioService::setLoopEnabled);
    connect(m_actions->action(Id::TransportMetronome), &QAction::toggled, m_audio, &AudioService::setMetronomeEnabled);
    connect(m_actions->action(Id::TransportPanic), &QAction::triggered, m_audio, &AudioService::panic);
    m_actions->action(Id::TransportLoop)->setChecked(true);
    m_actions->action(Id::TransportCountIn)->setChecked(m_settings->intValue(setting::Key::PracticeCountInBars) > 0);
    for (const Workspace &ws : m_workspaces) {
        connect(m_actions->action(ws.switchAction), &QAction::triggered, this, [this, id = ws.id] {
            switchTo(id);
        });
    }
    connect(m_audio, &AudioService::stateChanged, this, &Shell::showTransportState);
    connect(m_audio, &AudioService::slotChanged, m_session, &RoutineSession::setPlayingSlot);
    connect(m_audio, &AudioService::positionChanged, this, [this](std::int64_t ticks) {
        const std::int64_t length = m_audio->lengthTicks();
        const bool show = length > 0 && m_audio->isPlaying();
        m_timeline->setProgress(show ? static_cast<double>(ticks) / static_cast<double>(length) : -1.0);
        const bool copy = m_active == workspace::Id::Copy && m_scoreView->hasScore();
        m_scoreView->setPlayheadTick(copy && show ? std::optional<std::int64_t>(ticks) : std::nullopt);
    });
    connect(m_scoreView, &ScoreView::seekRequested, m_audio, &AudioService::seekToTick);
    connect(m_instrument, &InstrumentPanel::noteClicked, m_audio, &AudioService::clickNote);
    connect(m_timeline, &TimelineView::seekRequested, m_audio, &AudioService::seekToSlot);
    wireRoutineActions();
    wireRoutineNavigation();
    wireViewActions();
    wireSettingActions();
    wireTimerActions();
    wireCopyActions();
}

void Shell::wireCopyActions()
{
    connect(m_actions->action(Id::FileImport), &QAction::triggered, this, &Shell::importFile);
    wireCopyTransport();
    wireEditorActions();
    wireProjectActions();
    wireCopyDock();
    wireCopyInspector();
}

/// The file dialog is one of the three dialogs the app has (CLAUDE.md). Warnings are counted in the status
/// line and kept in the log; the score itself replaces whatever the view showed.
void Shell::importFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        tr("불러오기"),
        QString(),
        tr("음원·악보 (*.wav *.flac *.mp3 *.ogg *.musicxml *.xml);;음원 (*.wav *.flac *.mp3 *.ogg);;MusicXML "
           "(*.musicxml *.xml);;모든 파일 (*)"));
    if (file.isEmpty()) {
        return;
    }
    const std::filesystem::path path(file.toStdWString());
    const std::string extension = path.extension().string();
    if (extension == ".musicxml" || extension == ".xml") {
        importScore(path);
    } else {
        importAudio(path);
    }
}

/// A recording becomes a source version of the project and the player's clock master (audio-engine.md
/// section 15); without sync points the tick grid follows the score's tempo.
void Shell::importAudio(const std::filesystem::path &file)
{
    const Result<model::Id> id = m_project->importAudio(file);
    if (!id) {
        showStatus(errorText(id.error()));
        return;
    }
    if (m_active == workspace::Id::Copy) {
        pushCopyScore();
    } else {
        switchTo(workspace::Id::Copy);
    }
    showStatus(tr("음원 불러옴: %1").arg(QString::fromStdWString(file.filename().wstring())));
}

void Shell::importScore(const std::filesystem::path &file)
{
    model::IdGenerator ids;
    Result<score::ScoreImport> imported = score::importMusicXml(file, ids);
    if (!imported) {
        showStatus(errorText(imported.error()));
        return;
    }
    m_project->importScore(imported->score);
    m_scoreView->setScore(m_editor->score());
    if (m_active == workspace::Id::Copy) {
        pushCopyScore();
    } else {
        switchTo(workspace::Id::Copy);
    }
    showStatus(tr("악보 불러옴 · 마디 %1 · 트랙 %2 · 경고 %3")
                   .arg(imported->score.measures.size())
                   .arg(imported->score.tracks.size())
                   .arg(imported->warnings.size()));
}

void Shell::showTransportState(audio::EngineState state)
{
    const bool playing = state == audio::EngineState::Playing || state == audio::EngineState::CountIn;
    QAction *play = m_actions->action(Id::TransportPlayPause);
    play->setIcon(m_icons->icon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
    m_contextLabel->setText(playing ? tr("재생 중 · %1 BPM").arg(m_tempo->value()) : QString());
}

/// Copy playback: with a recording, the file is the clock (copy mode, tick grid = score tempos + sync points);
/// without one, the synth plays the score at its first tempo. Without a score the engine keeps silence.
void Shell::pushCopyScore()
{
    if (!m_scoreView->hasScore()) {
        return;
    }
    const model::Score &score = m_scoreView->score();
    m_audio->stop();
    m_audio->setTimeSignature(score.initialTimeSig.count, score.initialTimeSig.unit);
    m_audio->setTempo(score.initialTempoBpm);
    m_audio->setScore(score);
    m_audio->setSlotMask(~std::uint64_t{0});
    const auto sourcePath = m_project->activeSourcePath();
    const auto sourceId = m_project->activeSourceId();
    if (!sourcePath || !sourceId) {
        m_audio->setCopyMode(false);
        return;
    }
    if (const Result<void> r = m_audio->loadSources({*sourcePath}); !r) {
        showStatus(errorText(r.error()));
        m_audio->setCopyMode(false);
        return;
    }
    m_audio->setCopyTiming(score, project::syncPointsFor(m_project->project(), *sourceId));
    m_audio->setCopyMode(true);
    updateSpeedLabel();
}

void Shell::pushRoutineScore()
{
    const model::Score score = m_session->buildScore();
    m_audio->stop();
    m_audio->setCopyMode(false);
    m_audio->setTimeSignature(score.initialTimeSig.count, score.initialTimeSig.unit);
    m_audio->setTempo(m_session->state().tempoBpm);
    m_audio->setScore(score);
    m_audio->setSlotMask(m_session->audibleMask());
}

void Shell::wireRoutineActions()
{
    const auto pushScore = [this] {
        if (m_active == workspace::Id::Copy) {
            return; // the routine's score goes in again when its workspace comes back
        }
        m_audio->setScore(m_session->buildScore());
        m_audio->setSlotMask(m_session->audibleMask());
    };
    connect(m_session, &RoutineSession::slotsChanged, this, pushScore);
    const auto pushRootDisplay = [this] {
        m_session->setRootDisplay(rootDisplayOf(m_settings->stringValue(setting::Key::ViewFretboardRoot)));
    };
    connect(m_settings, &Settings::valueChanged, this, [pushRootDisplay](setting::Key key) {
        if (key == setting::Key::ViewFretboardRoot) {
            pushRootDisplay();
        }
    });
    pushRootDisplay();
    connect(m_session, &RoutineSession::playbackStyleChanged, this, [pushScore](routine::PlaybackStyle) {
        pushScore();
    });
    connect(m_session, &RoutineSession::mixChanged, this, [this] {
        m_audio->setSlotMask(m_session->audibleMask());
    });
    connect(m_actions->action(Id::RoutinePlaybackStyle), &QAction::triggered, this, [this] {
        const bool arp = m_session->playbackStyle() == routine::PlaybackStyle::Arp;
        m_session->setPlaybackStyle(arp ? routine::PlaybackStyle::Chord : routine::PlaybackStyle::Arp);
    });
    wireTempoAndKey();
    pushScore();
    m_audio->setTempo(m_session->state().tempoBpm);
}

void Shell::wireTempoAndKey()
{
    wireTempo();
    wireKey();
}

void Shell::wireTempo()
{
    connect(m_session, &RoutineSession::tempoChanged, this, [this](double bpm) {
        m_audio->setTempo(bpm);
        const QSignalBlocker blocker(m_tempo);
        m_tempo->setValue(static_cast<int>(bpm));
    });
    connect(m_tempo, &QSpinBox::valueChanged, this, [this](int bpm) {
        m_session->setTempo(bpm);
    });
    const auto step = [this](double delta) {
        m_session->setTempo(m_session->state().tempoBpm + delta);
    };
    connect(m_actions->action(Id::RoutineTempoUp), &QAction::triggered, this, [step] {
        step(1.0);
    });
    connect(m_actions->action(Id::RoutineTempoDown), &QAction::triggered, this, [step] {
        step(-1.0);
    });
    connect(m_actions->action(Id::RoutineTempoUp5), &QAction::triggered, this, [step] {
        step(5.0);
    });
    connect(m_actions->action(Id::RoutineTempoDown5), &QAction::triggered, this, [step] {
        step(-5.0);
    });
}

void Shell::wireKey()
{
    connect(m_key, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_session->setCurrentKey(m_key->itemData(index).toInt());
    });
    connect(m_session, &RoutineSession::slotsChanged, this, [this] {
        const QSignalBlocker blocker(m_key);
        m_key->setCurrentIndex(m_key->findData(m_session->state().currentKey));
    });
    m_key->setCurrentIndex(m_key->findData(m_session->state().currentKey));
    const auto keyStep = [this](int delta) {
        m_key->setCurrentIndex(wrapIndex(m_key->currentIndex() + delta, m_key->count()));
    };
    connect(m_actions->action(Id::RoutineKeyUp), &QAction::triggered, this, [keyStep] {
        keyStep(1);
    });
    connect(m_actions->action(Id::RoutineKeyDown), &QAction::triggered, this, [keyStep] {
        keyStep(-1);
    });
}

void Shell::wireRoutineNavigation()
{
    const auto select = [this](int delta) {
        const int count = static_cast<int>(m_session->slotList().size());
        if (count > 0) {
            m_session->setSelectedSlot(wrapIndex(m_session->selectedSlot() + delta, count));
        }
    };
    connect(m_actions->action(Id::RoutineChordPrev), &QAction::triggered, this, [select] {
        select(-1);
    });
    connect(m_actions->action(Id::RoutineChordNext), &QAction::triggered, this, [select] {
        select(1);
    });
    for (int n = 1; n <= RoutineSession::maxSlots; ++n) {
        connect(m_actions->action(Id::RoutineChordSelectN, n), &QAction::triggered, this, [this, n] {
            m_session->setSelectedSlot(n - 1);
        });
    }
    connect(m_actions->action(Id::RoutineChordMute), &QAction::triggered, this, [this] {
        m_session->toggleMute(m_session->selectedSlot());
    });
    connect(m_actions->action(Id::RoutineChordSolo), &QAction::triggered, this, [this] {
        m_session->toggleSolo(m_session->selectedSlot());
    });
    connect(m_actions->action(Id::RoutineChordMuteAll), &QAction::triggered, m_session, &RoutineSession::muteAll);
    connect(m_actions->action(Id::RoutineChordClearSolo), &QAction::triggered, m_session, &RoutineSession::clearSolo);
    for (int n = 1; n <= 4; ++n) {
        connect(m_actions->action(Id::RoutineModuleN, n), &QAction::triggered, this, [this, n] {
            m_session->setModule(QStringLiteral("0%1").arg(n).toStdString());
        });
    }
}

void Shell::wireViewActions()
{
    connect(m_actions->action(Id::ViewPanelMaximize), &QAction::triggered, this, [this](bool on) {
        if (on) {
            maximizePanel();
        } else {
            restorePanel();
        }
    });
    connect(m_actions->action(Id::ViewRestore), &QAction::triggered, this, &Shell::restorePanel);
    connect(m_actions->action(Id::ViewDockInstrument), &QAction::triggered, this, [this] {
        m_instrument->setVisible(!m_instrument->isVisible());
    });
    connect(m_actions->action(Id::ViewResetLayout), &QAction::triggered, this, &Shell::resetLayout);
    connect(m_actions->action(Id::AppCommandPalette), &QAction::triggered, this, [this] {
        m_palette->open();
    });
    connect(m_actions->action(Id::AppPreferences), &QAction::triggered, this, [this] {
        showPreferences(static_cast<int>(PreferencesPage::Appearance));
    });
    connect(m_actions->action(Id::AppKeymapEditor), &QAction::triggered, this, [this] {
        showPreferences(static_cast<int>(PreferencesPage::Shortcuts));
    });
    wireDockActions();
}

void Shell::wireDockActions()
{
    const auto toggleDock = [this](Id actionId, panel::Id panelId) {
        connect(m_actions->action(actionId), &QAction::triggered, this, [this, panelId](bool on) {
            setDockVisible(panelId, on);
        });
    };
    toggleDock(Id::ViewDockNavigator, panel::Id::DockNavigator);
    toggleDock(Id::ViewDockInspector, panel::Id::DockInspector);
    connect(m_actions->action(Id::ViewDockBottom), &QAction::triggered, this, [this](bool on) {
        setDockVisible(bottomPanel(), on);
    });
    toggleDock(Id::ViewDockStats, panel::Id::DockStats);
}

void Shell::wireTimerActions()
{
    connect(m_actions->action(Id::RoutineTimerToggle), &QAction::triggered, m_timer, &RoutineTimerController::toggle);
    connect(m_actions->action(Id::RoutineTimerReset), &QAction::triggered, m_timer, &RoutineTimerController::reset);
    connect(m_timer, &RoutineTimerController::changed, this, &Shell::updateTimerChip);
    connect(m_timer, &RoutineTimerController::segmentFinished, this, [this](int module) {
        showStatus(tr("%1 · %2분 완료").arg(m_timer->moduleLabel(module)).arg(m_timer->lengthSeconds(module) / 60));
        if (!isActiveWindow()) {
            QApplication::alert(this); // states.md section 7: once, no sound; the notification-center toast is TODO
        }
    });
    updateTimerChip();
}

/// Toolbar chip "mm:ss · module" (components.md section 2.1): paused shows the secondary text colour.
void Shell::updateTimerChip()
{
    const routine::TimerState state = m_timer->timer().state();
    m_timerChip->setText(m_timer->remainingText() + QStringLiteral(" · ")
                         + m_timer->moduleLabel(m_timer->currentModule()));
    m_timerChip->setToolTip(state == routine::TimerState::Running ? tr("타이머 일시정지") : tr("타이머 시작"));
    QPalette palette = m_timerChip->palette();
    const bool dimmed = state == routine::TimerState::Paused || state == routine::TimerState::Finished;
    palette.setColor(QPalette::ButtonText,
                     m_theme->color(dimmed ? theme::Token::ContentColorSecondary : theme::Token::ContentColorDefault));
    m_timerChip->setPalette(palette);
}

void Shell::wireSettingActions()
{
    connect(m_actions->action(Id::InstrumentKeyboardStrip), &QAction::triggered, this, [this] {
        const bool on = m_settings->boolValue(setting::Key::ViewKeyboardStrip);
        m_settings->setValue(setting::Key::ViewKeyboardStrip, nlohmann::json(!on));
    });
    const auto toggleSetting = [this](Id actionId, setting::Key key, const char *a, const char *b) {
        connect(m_actions->action(actionId), &QAction::triggered, this, [this, key, a, b] {
            const bool isA = m_settings->stringValue(key) == QLatin1String(a);
            m_settings->setValue(key, QString(QLatin1String(isA ? b : a)));
        });
    };
    toggleSetting(Id::FretboardLabel, setting::Key::ViewFretboardLabel, "degree", "name");
    toggleSetting(Id::FretboardColor, setting::Key::ViewFretboardColor, "chord", "degree");
    const auto themeAction = [this](Id actionId, const char *value) {
        connect(m_actions->action(actionId), &QAction::triggered, this, [this, value] {
            m_settings->setValue(setting::Key::AppearanceTheme, QString(QLatin1String(value)));
        });
    };
    themeAction(Id::ViewThemeDark, "dark");
    themeAction(Id::ViewThemeLight, "light");
    themeAction(Id::ViewThemeSystem, "system");
    const QString theme = m_settings->stringValue(setting::Key::AppearanceTheme);
    Id checked = Id::ViewThemeSystem;
    if (theme == QStringLiteral("light")) {
        checked = Id::ViewThemeLight;
    } else if (theme == QStringLiteral("dark")) {
        checked = Id::ViewThemeDark;
    }
    m_actions->action(checked)->setChecked(true);
}

} // namespace cc::ui
