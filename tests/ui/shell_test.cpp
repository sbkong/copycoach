// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Shell smoke test on the offscreen platform: the window builds from the registry, docks carry their panel ids,
// workspaces switch, panel maximize / restore works, actions drive the session.
#include "core/media/audiofile.h"
#include "core/predict/chordminimodel.h"
#include "core/project/predictioncache.h"
#include "core/separation/spleetermodel.h"
#include "core/stats/event.h"
#include "generated/actionids.h"
#include "generated/panelids.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/copybottomdock.h"
#include "ui/copy/noteroll.h"
#include "ui/copy/stemmixer.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/routine/routinesession.h"
#include "ui/routine/routinetimer.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/keymap.h"
#include "ui/shell/settings.h"
#include "ui/shell/shell.h"
#include "ui/theme/style.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QTest>
#include <QUrl>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace cc;
using namespace cc::ui;

namespace
{

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

routine::RoutineState shellLoopState()
{
    routine::RoutineState state;
    state.moduleId = "02";
    state.modeId = "diatonic-loop";
    return state;
}

struct CollectingSink final : stats::IStatsSink {
    std::vector<stats::StatsEvent> events;

    void record(const stats::StatsEvent &event) override
    {
        events.push_back(event);
    }
};

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "shell";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    Theme theme;
    ActionRegistry actions;
    Keymap keymap{userDir};
    RoutineSession session{routine::builtInRoutineDefinition(), shellLoopState()};

    Fixture()
    {
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(paths.theme(true)));
        QApplication::setStyle(new CopyCoachStyle(theme)); // NOLINT(cppcoreguidelines-owning-memory) app owns it
    }
};

} // namespace

TEST_CASE("shell: builds from the registry and the docks are named by panel id")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    QTest::qWait(50);

    REQUIRE(shell.menuBar()->actions().size() == 8);
    REQUIRE(shell.minimumHeight() == 660);
    const auto docks = shell.findChildren<QDockWidget *>();
    REQUIRE(docks.size() == 7); // navigator, inspector, timeline, stats, audio track, video, stem mixer
    for (const QDockWidget *dock : docks) {
        bool known = false;
        for (const panel::Info &info : panel::infos) {
            known = known || dock->objectName() == fromView(info.id);
        }
        INFO(dock->objectName().toStdString());
        REQUIRE(known);
    }
    REQUIRE(f.actions.action(action::Id::WorkspaceRoutine)->isChecked());
    REQUIRE(f.actions.action(action::Id::RoutineChordNext)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::CopySpeedDown)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::FilePrint)->isEnabled()); // not implemented yet: disabled, not hidden
    REQUIRE(f.actions.action(action::Id::FileNew)->isEnabled());
    REQUIRE(f.actions.action(action::Id::EditUndo)->isEnabled());
    // wired actions must not sit in the not-yet-implemented list (piece 9 and 10 regressions)
    REQUIRE(f.actions.action(action::Id::AppCommandPalette)->isEnabled());
    REQUIRE(f.actions.action(action::Id::AppKeymapEditor)->isEnabled());
    REQUIRE(f.actions.action(action::Id::AppPreferences)->isEnabled());
    REQUIRE(f.actions.action(action::Id::ViewDockStats)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::ViewStatsFull)->isEnabled());
    // no reader: the status bar still shows today's minutes, as zero (stats.md section 3)
    QLabel *today = nullptr;
    for (QLabel *label : shell.findChildren<QLabel *>()) {
        if (label->accessibleName() == QStringLiteral("오늘 연습 시간")) {
            today = label;
        }
    }
    REQUIRE(today != nullptr);
    REQUIRE(today->text() == QStringLiteral("오늘 0분"));
}

TEST_CASE("shell: closing while the timer runs pauses it first, so the stop event carries the seconds")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    CollectingSink sink;
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap, &sink});
    shell.show();
    QTest::qWait(50);
    f.actions.action(action::Id::RoutineTimerToggle)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Running);
    shell.timer()->advance(70);
    REQUIRE(sink.events.size() == 2); // the start and the first minute's tick
    REQUIRE(shell.close());
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Paused);
    REQUIRE(sink.events.size() == 3);
    REQUIRE(sink.events.back().type == stats::event::routineModuleStop);
    REQUIRE(sink.events.back().payload["elapsed"] == 70);
}

TEST_CASE("shell: workspace switch, panel maximize and routine actions")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    QTest::qWait(50);

    f.actions.action(action::Id::WorkspaceCopy)->trigger();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Copy);
    REQUIRE(f.actions.action(action::Id::CopySpeedDown)->isEnabled());
    REQUIRE_FALSE(f.actions.action(action::Id::RoutineChordNext)->isEnabled());
    f.actions.action(action::Id::WorkspaceRoutine)->trigger();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Routine);

    f.actions.action(action::Id::ViewPanelMaximize)->trigger();
    REQUIRE(shell.isPanelMaximized());
    for (const QDockWidget *dock : shell.findChildren<QDockWidget *>()) {
        REQUIRE(dock->isHidden());
    }
    f.actions.action(action::Id::ViewRestore)->trigger();
    REQUIRE_FALSE(shell.isPanelMaximized());

    REQUIRE(f.session.selectedSlot() == 0);
    f.actions.action(action::Id::RoutineChordNext)->trigger();
    REQUIRE(f.session.selectedSlot() == 1);
    f.actions.action(action::Id::RoutineChordSelectN, 5)->trigger();
    REQUIRE(f.session.selectedSlot() == 4);
    f.actions.action(action::Id::RoutineTempoUp5)->trigger();
    REQUIRE(f.session.state().tempoBpm == 65.0);
    f.actions.action(action::Id::RoutineModuleN, 3)->trigger();
    REQUIRE(f.session.state().moduleId == "03");
    f.actions.action(action::Id::FretboardLabel)->trigger();
    REQUIRE(f.settings.stringValue(setting::Key::ViewFretboardLabel) == QStringLiteral("name"));
    f.actions.action(action::Id::RoutineTimerToggle)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Running);
    REQUIRE(shell.timer()->currentModule() == 2); // module 03 entered above
    REQUIRE(shell.timer()->timer().lengthSeconds() == 600);
    shell.timer()->advance(600);
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Finished);
    f.actions.action(action::Id::RoutineTimerReset)->trigger();
    REQUIRE(shell.timer()->timer().state() == routine::TimerState::Idle);
    REQUIRE(shell.timer()->timer().elapsedSeconds(2) == 600);
    f.actions.action(action::Id::TransportPlayPause)->trigger();
    QTest::qWait(120);
    REQUIRE(audio.isPlaying());
    f.actions.action(action::Id::TransportPanic)->trigger();
    QTest::qWait(120);
    REQUIRE_FALSE(audio.isPlaying());
}

TEST_CASE("shell: renders dark and light screenshots into the build folder")
{
    // Not an assertion about pixels: the images under <build>/shots/ are for eyes (mockup M2 comparison).
    for (const bool dark : {true, false}) {
        Fixture f;
        REQUIRE(f.theme.load(f.paths.theme(dark)));
        QApplication::setPalette(f.theme.palette(Surface::Canvas));
        AudioService audio(f.paths, f.settings);
        Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
        f.session.setOption("upper", "on");
        shell.resize(1280, 720);
        shell.show();
        QTest::qWait(100);
        const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
        REQUIRE(shots.mkpath(QStringLiteral(".")));
        const QString file =
            shots.filePath(dark ? QStringLiteral("shell-dark.png") : QStringLiteral("shell-light.png"));
        REQUIRE(shell.grab().save(file));
    }
}

TEST_CASE("shell: key prediction offers the key and cents inline and accept writes both as edits")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    // four seconds of a D major triad with harmonics, A4 = 440 Hz
    const std::filesystem::path wav = f.userDir / "dmajor.wav";
    {
        constexpr int rate = 44100;
        std::vector<float> stereo(static_cast<std::size_t>(rate) * 4 * 2, 0.0F);
        for (std::size_t i = 0; i < stereo.size() / 2; ++i) {
            const double t = static_cast<double>(i) / rate;
            double v = 0.0;
            for (const int midi : {50, 54, 57, 62}) { // D F# A d
                const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
                for (int h = 1; h <= 3; ++h) {
                    v += std::sin(2.0 * std::numbers::pi * f0 * h * t) / (h * h);
                }
            }
            stereo[i * 2] = static_cast<float>(0.1 * v);
            stereo[(i * 2) + 1] = stereo[i * 2];
        }
        media::AudioFileWriter writer;
        REQUIRE(writer.open(wav, rate, 2, media::AudioFileFormat::WavFloat));
        REQUIRE(writer.write(stereo));
        REQUIRE(writer.close());
    }
    const QString scoreFile = QString::fromStdWString(
        (std::filesystem::path(CC_FIXTURE_DIR) / "score" / "import-features.musicxml").wstring());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(scoreFile), QUrl::fromLocalFile(QString::fromStdWString(wav.wstring()))});
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &enter);
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &drop);
    REQUIRE(drop.isAccepted());

    f.actions.action(action::Id::CopyPredictKey)->trigger(); // inline: no job queue in this shell
    auto *message = shell.findChild<InlineMessage *>();
    REQUIRE(message != nullptr);
    REQUIRE(message->isVisibleTo(&shell));
    const QLabel *text = message->findChild<QLabel *>();
    REQUIRE(text != nullptr);
    REQUIRE(text->text().contains(QStringLiteral("D major")));
    QAbstractButton *accept = nullptr;
    for (QAbstractButton *button : message->findChildren<QAbstractButton *>()) {
        if (button->text() == QStringLiteral("받아들이기")) {
            accept = button;
        }
    }
    REQUIRE(accept != nullptr);
    accept->click();
    REQUIRE(shell.statusBar()->currentMessage().contains(QStringLiteral("키 D major"))); // both edits went in
    REQUIRE_FALSE(message->isVisibleTo(&shell));

    // the same recording through Basic Pitch: the grey roll fills, accept writes notes into the score
    f.actions.action(action::Id::CopyPredictNotes)->trigger();
    auto *dock = shell.findChild<CopyBottomDock *>();
    REQUIRE(dock != nullptr);
    REQUIRE_FALSE(dock->noteRoll()->notes().empty());
    REQUIRE(f.actions.action(action::Id::CopyPredictAccept)->isEnabled());
    f.actions.action(action::Id::CopyPredictAccept)->trigger();
    REQUIRE(shell.statusBar()->currentMessage().contains(QStringLiteral("받아들임")));
    // D-3: the shown layer went to cache/predictions/<id>.json and comes back as the layer of that version
    const std::filesystem::path cacheDir = std::filesystem::temp_directory_path() / "copycoach-cache" / "predictions";
    bool cached = false;
    for (const auto &entry : std::filesystem::directory_iterator(cacheDir)) {
        const Result<project::PredictionCache> back = project::loadPredictionCache(entry.path());
        cached = cached || (back && back->notes && !back->notes->empty());
    }
    REQUIRE(cached);

    // and through ChordMini when the exported model is around: a grey D above the score, accept writes it
    const std::filesystem::path chordModel = std::filesystem::path(CC_CHORDMINI_DIR) / predict::chordMiniModel.name;
    if (!std::filesystem::exists(chordModel)) {
        SKIP("chordmini-2e1d.onnx not exported (tools/models/export_chordmini.py)");
    }
    const std::filesystem::path modelDir = f.userDir / "models" / "chordmini";
    std::filesystem::create_directories(modelDir);
    std::filesystem::copy_file(chordModel,
                               modelDir / chordModel.filename(),
                               std::filesystem::copy_options::overwrite_existing);
    f.actions.action(action::Id::CopyPredictChords)->trigger();
    auto *scoreView = shell.findChild<ScoreView *>();
    REQUIRE(scoreView != nullptr);
    REQUIRE_FALSE(scoreView->session().predictedChords.empty());
    REQUIRE(scoreView->session().predictedChords.front().text.starts_with('D'));
    f.actions.action(action::Id::CopyPredictAccept)->trigger();
    REQUIRE(shell.statusBar()->currentMessage().contains(QStringLiteral("예측 코드 받아들임")));
    REQUIRE_FALSE(scoreView->score().measures.front().chords.empty());
    REQUIRE(scoreView->session().predictedChords.empty()); // every measure was accepted, the layer is gone
}

TEST_CASE("shell: stem separation replaces the recording with its stems and the mixer drives them")
{
    const std::filesystem::path stemModel = std::filesystem::path(CC_SPLEETER_DIR) / separation::spleeterModel.name;
    if (!std::filesystem::exists(stemModel)) {
        SKIP("spleeter-4stems.onnx not exported (tools/models/export_spleeter.py)");
    }
    Fixture f;
    const std::filesystem::path modelDir = f.userDir / "models" / "spleeter";
    std::filesystem::create_directories(modelDir);
    std::filesystem::copy_file(stemModel,
                               modelDir / stemModel.filename(),
                               std::filesystem::copy_options::overwrite_existing);
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    const std::filesystem::path wav = f.userDir / "bassline.wav";
    {
        constexpr int rate = 44100;
        std::vector<float> stereo(static_cast<std::size_t>(rate) * 2 * 2, 0.0F);
        for (std::size_t i = 0; i < stereo.size() / 2; ++i) {
            const double t = static_cast<double>(i) / rate;
            const auto v = static_cast<float>(0.4 * std::sin(2.0 * std::numbers::pi * 55.0 * t));
            stereo[i * 2] = v;
            stereo[(i * 2) + 1] = v;
        }
        media::AudioFileWriter writer;
        REQUIRE(writer.open(wav, rate, 2, media::AudioFileFormat::WavFloat));
        REQUIRE(writer.write(stereo));
        REQUIRE(writer.close());
    }
    const QString scoreFile = QString::fromStdWString(
        (std::filesystem::path(CC_FIXTURE_DIR) / "score" / "import-features.musicxml").wstring());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(scoreFile), QUrl::fromLocalFile(QString::fromStdWString(wav.wstring()))});
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &enter);
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &drop);
    REQUIRE(drop.isAccepted());
    REQUIRE(audio.sourceCount() == 1);
    auto *mixer = shell.findChild<StemMixerPanel *>();
    REQUIRE(mixer != nullptr);
    REQUIRE_FALSE(mixer->hasStems());

    f.actions.action(action::Id::CopyStemSeparate)->trigger(); // inline: no job queue in this shell
    REQUIRE(shell.statusBar()->currentMessage().contains(QStringLiteral("스템 분리 완료 · Spleeter")));
    REQUIRE(audio.sourceCount() == 4);
    REQUIRE(mixer->stemCount() == 4);
    f.actions.action(action::Id::CopyStemMuteN, 2)->trigger();
    REQUIRE(mixer->isMuted(1));
    REQUIRE_FALSE(mixer->isMuted(0));
}

TEST_CASE("shell: a MusicXML file dropped on the window is imported into the copy workspace")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    Shell shell(ShellContext{&f.paths, &f.theme, &f.settings, &f.actions, &audio, &f.session, &f.keymap});
    shell.show();
    REQUIRE(shell.activeWorkspace() == workspace::Id::Routine);
    const QString fixture = QString::fromStdWString(
        (std::filesystem::path(CC_FIXTURE_DIR) / "score" / "import-features.musicxml").wstring());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(fixture)});
    QDragEnterEvent enter(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &enter);
    REQUIRE(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &drop);
    REQUIRE(drop.isAccepted());
    REQUIRE(shell.activeWorkspace() == workspace::Id::Copy);
    REQUIRE(shell.windowTitle().contains(QStringLiteral("저장되지 않은 프로젝트")));

    // a video file: a video version, its audio extracted inline (no job queue here), the video dock shown
    const QString video =
        QString::fromStdWString((std::filesystem::path(CC_FIXTURE_DIR) / "video" / "gray-steps.mp4").wstring());
    QMimeData videoMime;
    videoMime.setUrls({QUrl::fromLocalFile(video)});
    QDragEnterEvent videoEnter(QPointF(10, 10), Qt::CopyAction, &videoMime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &videoEnter);
    REQUIRE(videoEnter.isAccepted());
    QDropEvent videoDrop(QPointF(10, 10), Qt::CopyAction, &videoMime, Qt::LeftButton, Qt::NoModifier);
    REQUIRE(shell.findChild<QDockWidget *>(QStringLiteral("dock.video"))->isHidden());
    QApplication::sendEvent(&shell, &videoDrop);
    REQUIRE(videoDrop.isAccepted());
    REQUIRE_FALSE(shell.findChild<QDockWidget *>(QStringLiteral("dock.video"))->isHidden());
    REQUIRE(f.actions.action(action::Id::ViewDockVideo)->isEnabled());
    REQUIRE(audio.copyMode()); // the extracted audio was loaded as the recording

    QMimeData other;
    other.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/nope.txt"))});
    QDragEnterEvent refused(QPointF(10, 10), Qt::CopyAction, &other, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&shell, &refused);
    REQUIRE_FALSE(refused.isAccepted());
}
