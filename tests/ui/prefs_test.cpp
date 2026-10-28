// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Preferences dialog (ia.md section 7): nine pages, the three written ones read and write the settings, the
// audio page applies through the service and reports a device failure inline (offscreen has no device).
#include "ui/audio/audioservice.h"
#include "ui/prefs/appearancepage.h"
#include "ui/prefs/audiodevicepage.h"
#include "ui/prefs/practicepage.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/keymap.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QDir>
#include <QListWidget>
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

TEST_CASE("preferences: renders the three written pages into the build folder")
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
             {PreferencesPage::Practice, QStringLiteral("prefs-practice.png")}}) {
        dialog.showPage(page);
        QTest::qWait(30);
        REQUIRE(dialog.grab().save(shots.filePath(name)));
    }
}
