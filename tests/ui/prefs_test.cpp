// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Preferences dialog (ia.md section 7): nine pages, the three written ones read and write the settings, the
// audio page applies through the service and reports a device failure inline (offscreen has no device).
#include "ui/audio/audioservice.h"
#include "ui/prefs/appearancepage.h"
#include "ui/prefs/audiodevicepage.h"
#include "ui/prefs/practicepage.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/prefs/separationpage.h"
#include "ui/prefs/statspage.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/keymap.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>
#include <filesystem>

using namespace cc;
using namespace cc::ui;

namespace
{

struct Fixture {
    std::filesystem::path userDir = std::filesystem::temp_directory_path() / "copycoach-tests" / "prefs";
    AppPaths paths{std::filesystem::path(CC_DATA_DIR), std::filesystem::path(CC_SOUND_DIR), userDir};
    Settings settings{userDir};
    Theme theme;
    ActionRegistry actions;
    Keymap keymap{userDir};

    Fixture()
    {
        std::filesystem::create_directories(userDir);
        REQUIRE(theme.load(paths.theme(true)));
    }
};

QToolButton *buttonNamed(const QWidget *root, const QString &accessibleName)
{
    for (QToolButton *button : root->findChildren<QToolButton *>()) {
        if (button->accessibleName() == accessibleName) {
            return button;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("preferences: nine pages, apply buttons only on the audio page")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &audio, &f.actions, &f.keymap});
    dialog.show();
    QTest::qWait(30);
    REQUIRE(dialog.findChild<QListWidget *>()->count() == static_cast<int>(PreferencesPage::Count));
    REQUIRE(dialog.currentPage() == PreferencesPage::AudioDevice);
    REQUIRE(dialog.page(PreferencesPage::AudioDevice)->needsApply());
    dialog.showPage(PreferencesPage::Practice);
    REQUIRE(dialog.currentPage() == PreferencesPage::Practice);
    REQUIRE_FALSE(dialog.page(PreferencesPage::Practice)->needsApply());
}

TEST_CASE("preferences: appearance and practice pages write the settings immediately")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &audio, &f.actions, &f.keymap});
    dialog.show();
    QTest::qWait(30);

    QToolButton *light = buttonNamed(dialog.page(PreferencesPage::Appearance), QStringLiteral("테마 라이트"));
    REQUIRE(light != nullptr);
    light->click();
    REQUIRE(f.settings.stringValue(setting::Key::AppearanceTheme) == QStringLiteral("light"));
    QToolButton *large = buttonNamed(dialog.page(PreferencesPage::Appearance), QStringLiteral("지판 점 크기 크게"));
    REQUIRE(large != nullptr);
    large->click();
    REQUIRE(f.settings.stringValue(setting::Key::AppearanceFretboardDotSize) == QStringLiteral("large"));
    dialog.page(PreferencesPage::Appearance)->restoreDefaults();
    REQUIRE(f.settings.stringValue(setting::Key::AppearanceTheme) == QStringLiteral("system"));

    const auto *practice = dialog.page(PreferencesPage::Practice);
    QSpinBox *countIn = nullptr;
    for (QSpinBox *spin : practice->findChildren<QSpinBox *>()) {
        if (spin->accessibleName() == QStringLiteral("카운트인 마디 수")) {
            countIn = spin;
        }
    }
    REQUIRE(countIn != nullptr);
    countIn->setValue(2);
    REQUIRE(f.settings.intValue(setting::Key::PracticeCountInBars) == 2);
    QComboBox *metronome = nullptr;
    for (QComboBox *combo : practice->findChildren<QComboBox *>()) {
        if (combo->accessibleName() == QStringLiteral("메트로놈 소리")) {
            metronome = combo;
        }
    }
    REQUIRE(metronome != nullptr);
    REQUIRE(metronome->count() == 4); // the four synthesized sets from the catalog
    metronome->setCurrentIndex(metronome->findData(QStringLiteral("beep")));
    REQUIRE(f.settings.stringValue(setting::Key::PracticeMetronomeSound) == QStringLiteral("beep"));
}

TEST_CASE("preferences: the audio page applies through the service and shows the failure inline")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &audio, &f.actions, &f.keymap});
    dialog.show();
    QTest::qWait(30);
    auto *page = dynamic_cast<AudioDevicePage *>(
        dialog.page(PreferencesPage::AudioDevice)); // NOLINT(cppcoreguidelines-pro-type-dynamic-cast)
    REQUIRE(page != nullptr);
    REQUIRE(page->hostApi() == QStringLiteral("wasapi-shared"));
    QToolButton *asio = buttonNamed(page, QStringLiteral("오디오 시스템 ASIO"));
    REQUIRE(asio != nullptr);
    asio->click();
    REQUIRE(page->hostApi() == QStringLiteral("asio"));
    page->discard();
    REQUIRE(page->hostApi() == QStringLiteral("wasapi-shared"));
    asio->click();
    page->apply();
    REQUIRE(f.settings.stringValue(setting::Key::AudioBackend) == QStringLiteral("asio"));
    // offscreen: no ASIO driver -> the service falls back to silence and the page says so inline
    REQUIRE_FALSE(audio.hasDevice());
    REQUIRE(page->hasMessage());
    page->restoreDefaults();
    REQUIRE(f.settings.stringValue(setting::Key::AudioBackend) == QStringLiteral("wasapi-shared"));
}

TEST_CASE("preferences: the stats page round-trips its four settings and shows the effective database path")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &audio, &f.actions, &f.keymap, &f.paths});
    dialog.show();
    QTest::qWait(30);
    auto *page = dynamic_cast<StatsPage *>(
        dialog.page(PreferencesPage::Stats)); // NOLINT(cppcoreguidelines-pro-type-dynamic-cast)
    REQUIRE(page != nullptr);
    REQUIRE_FALSE(page->needsApply());
    REQUIRE(page->effectiveDbPath() == QString::fromStdWString((f.userDir / "stats.db").wstring()));

    auto *enabled = page->findChild<QCheckBox *>();
    REQUIRE(enabled != nullptr);
    REQUIRE(enabled->isChecked());
    enabled->setChecked(false);
    REQUIRE_FALSE(f.settings.boolValue(setting::Key::StatsEnabled));
    QSpinBox *minDay = nullptr;
    QSpinBox *retention = nullptr;
    for (QSpinBox *spin : page->findChildren<QSpinBox *>()) {
        if (spin->accessibleName() == QStringLiteral("하루 인정 최소 분")) {
            minDay = spin;
        } else if (spin->accessibleName() == QStringLiteral("보존 기간 일")) {
            retention = spin;
        }
    }
    REQUIRE(minDay != nullptr);
    REQUIRE(retention != nullptr);
    REQUIRE(minDay->value() == 5);
    REQUIRE(retention->value() == 0);
    REQUIRE(retention->text() == QStringLiteral("무제한"));
    minDay->setValue(10);
    retention->setValue(90);
    REQUIRE(f.settings.intValue(setting::Key::StatsMinDayMinutes) == 10);
    REQUIRE(f.settings.intValue(setting::Key::StatsRetentionDays) == 90);

    f.settings.setValue(setting::Key::StatsDbPath, QStringLiteral("D:/records/stats.db")); // the page follows
    REQUIRE(page->effectiveDbPath() == QStringLiteral("D:/records/stats.db"));
    page->restoreDefaults();
    REQUIRE(f.settings.boolValue(setting::Key::StatsEnabled));
    REQUIRE(minDay->value() == 5);
    REQUIRE(retention->value() == 0);
    REQUIRE(f.settings.stringValue(setting::Key::StatsDbPath).isEmpty());
}

TEST_CASE("preferences: the score, separation, files and video pages write their settings immediately")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &audio, &f.actions, &f.keymap, &f.paths});
    dialog.show();
    QTest::qWait(30);

    PreferencesPageBase *score = dialog.page(PreferencesPage::ScoreTab);
    REQUIRE_FALSE(score->needsApply());
    for (QComboBox *combo : score->findChildren<QComboBox *>()) {
        if (combo->accessibleName() == QStringLiteral("표기 기본")) {
            combo->setCurrentIndex(1);
        }
    }
    REQUIRE(f.settings.stringValue(setting::Key::ScoreNotation) == QStringLiteral("tab"));
    for (QSpinBox *spin : score->findChildren<QSpinBox *>()) {
        spin->setValue(3);
    }
    REQUIRE(f.settings.intValue(setting::Key::ScoreDefaultCapo) == 3);
    score->restoreDefaults();
    REQUIRE(f.settings.intValue(setting::Key::ScoreDefaultCapo) == 0);
    REQUIRE(f.settings.stringValue(setting::Key::ScoreNotation) == QStringLiteral("staff-tab"));

    auto *separation = dynamic_cast<SeparationPage *>(
        dialog.page(PreferencesPage::StemSeparation)); // NOLINT(cppcoreguidelines-pro-type-dynamic-cast)
    REQUIRE(separation != nullptr);
    REQUIRE(separation->beatModelStatus().startsWith(QStringLiteral("없음"))); // the test user folder has no models
    for (const QPushButton *button : separation->findChildren<QPushButton *>()) {
        if (button->accessibleName() == QStringLiteral("박 예측 모델 내려받기")) {
            REQUIRE_FALSE(button->isEnabled()); // no job queue in this context
        }
    }
    for (QComboBox *combo : separation->findChildren<QComboBox *>()) {
        if (combo->accessibleName() == QStringLiteral("박 예측 모델 선택")) {
            combo->setCurrentIndex(1);
        }
    }
    REQUIRE(f.settings.stringValue(setting::Key::CopyPredictBeatModel) == QStringLiteral("small"));
    for (QLineEdit *edit : separation->findChildren<QLineEdit *>()) {
        if (edit->placeholderText() == QStringLiteral("앱 데이터 폴더")) {
            edit->setText(QStringLiteral("D:/models"));
            emit edit->editingFinished();
        }
    }
    REQUIRE(f.settings.stringValue(setting::Key::SeparationModelDir) == QStringLiteral("D:/models"));
    separation->restoreDefaults();
    REQUIRE(f.settings.stringValue(setting::Key::SeparationModelDir).isEmpty());
    REQUIRE(f.settings.stringValue(setting::Key::CopyPredictBeatModel) == QStringLiteral("final"));

    PreferencesPageBase *files = dialog.page(PreferencesPage::Files);
    for (QSpinBox *spin : files->findChildren<QSpinBox *>()) {
        if (spin->accessibleName() == QStringLiteral("자동 저장 간격")) {
            spin->setValue(120);
        }
    }
    REQUIRE(f.settings.intValue(setting::Key::FilesAutosaveIntervalS) == 120);
    auto *copyMedia = files->findChild<QCheckBox *>();
    REQUIRE(copyMedia != nullptr);
    copyMedia->setChecked(false);
    REQUIRE_FALSE(f.settings.boolValue(setting::Key::FilesCopyMediaIntoProject));
    files->restoreDefaults();
    REQUIRE(f.settings.intValue(setting::Key::FilesAutosaveIntervalS) == 60);
    REQUIRE(f.settings.boolValue(setting::Key::FilesCopyMediaIntoProject));

    PreferencesPageBase *video = dialog.page(PreferencesPage::Video);
    auto *hw = video->findChild<QCheckBox *>();
    REQUIRE(hw != nullptr);
    hw->setChecked(false);
    REQUIRE_FALSE(f.settings.boolValue(setting::Key::VideoHwDecode));
    for (QSpinBox *spin : video->findChildren<QSpinBox *>()) {
        spin->setValue(1024);
    }
    REQUIRE(f.settings.intValue(setting::Key::VideoFrameCacheMb) == 1024);
    video->restoreDefaults();
    REQUIRE(f.settings.boolValue(setting::Key::VideoHwDecode));
    REQUIRE(f.settings.intValue(setting::Key::VideoFrameCacheMb) == 512);
}

TEST_CASE("preferences: renders the written pages into the build folder")
{
    Fixture f;
    AudioService audio(f.paths, f.settings);
    PreferencesDialog dialog(PreferencesContext{&f.theme, &f.settings, &audio, &f.actions, &f.keymap});
    dialog.show();
    QTest::qWait(50);
    const QDir shots(QCoreApplication::applicationDirPath() + QStringLiteral("/shots"));
    REQUIRE(shots.mkpath(QStringLiteral(".")));
    for (const auto &[page, name] : std::vector<std::pair<PreferencesPage, QString>>{
             {PreferencesPage::AudioDevice, QStringLiteral("prefs-audio.png")},
             {PreferencesPage::Appearance, QStringLiteral("prefs-appearance.png")},
             {PreferencesPage::Practice, QStringLiteral("prefs-practice.png")},
             {PreferencesPage::Stats, QStringLiteral("prefs-stats.png")},
             {PreferencesPage::ScoreTab, QStringLiteral("prefs-score.png")},
             {PreferencesPage::StemSeparation, QStringLiteral("prefs-separation.png")},
             {PreferencesPage::Files, QStringLiteral("prefs-files.png")},
             {PreferencesPage::Video, QStringLiteral("prefs-video.png")}}) {
        dialog.showPage(page);
        QTest::qWait(30);
        REQUIRE(dialog.grab().save(shots.filePath(name)));
    }
}
