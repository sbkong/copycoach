// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Composition root: paths, theme, style, settings, routine data, audio, shell. Nothing else news these objects.
#include "core/routine/definition.h"
#include "core/routine/state.h"
#include "core/stats/sqlitestore.h"
#include "core/version.h"
#include "generated/settingkeys.h"
#include "ui/audio/audioservice.h"
#include "ui/jobs/jobservice.h"
#include "ui/routine/routinesession.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/apppaths.h"
#include "ui/shell/errortext.h"
#include "ui/shell/keymap.h"
#include "ui/shell/settings.h"
#include "ui/shell/shell.h"
#include "ui/theme/style.h"
#include "ui/theme/theme.h"

#include <QApplication>
#include <QLoggingCategory>
#include <QStyleHints>

#include <filesystem>
#include <memory>
#include <vector>

namespace
{

Q_LOGGING_CATEGORY(logApp, "cc.app")

bool wantsDarkTheme(const cc::ui::Settings &settings)
{
    const QString theme = settings.stringValue(cc::setting::Key::AppearanceTheme);
    if (theme == QStringLiteral("light")) {
        return false;
    }
    if (theme == QStringLiteral("dark")) {
        return true;
    }
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

cc::routine::RoutineDefinition loadDefinition(const cc::ui::AppPaths &paths, std::vector<QString> &messages)
{
    auto definition = cc::routine::loadRoutineDefinition(paths.dataDir / "routines" / "default.json");
    if (definition) {
        return *definition;
    }
    messages.push_back(cc::ui::errorText(definition.error()));
    return cc::routine::builtInRoutineDefinition();
}

cc::routine::RoutineState
loadState(const cc::ui::AppPaths &paths, const cc::ui::Settings &settings, std::vector<QString> &messages)
{
    auto state = cc::routine::loadRoutineState(paths.userDir / "routine-state.json");
    if (state) {
        return *state;
    }
    if (state.error().code != cc::ErrorCode::FileNotFound) {
        messages.push_back(cc::ui::errorText(state.error()));
    }
    cc::routine::RoutineState fresh;
    fresh.tempoBpm = settings.intValue(cc::setting::Key::PracticeTempoDefault);
    fresh.metronomeSound = settings.stringValue(cc::setting::Key::PracticeMetronomeSound).toStdString();
    fresh.guitarSound = settings.stringValue(cc::setting::Key::PracticeGuitarSound).toStdString();
    fresh.pianoSound = settings.stringValue(cc::setting::Key::PracticePianoSound).toStdString();
    return fresh;
}

/// stats.db in the user folder (or stats.db-path) with one session for this run, when stats.enabled;
/// nullptr when recording is off or the file cannot be opened (the message says so, practice goes on unrecorded).
std::unique_ptr<cc::stats::SqliteStatsStore>
openStats(const cc::ui::AppPaths &paths, const cc::ui::Settings &settings, std::vector<QString> &messages)
{
    if (!settings.boolValue(cc::setting::Key::StatsEnabled)) {
        return nullptr;
    }
    const QString custom = settings.stringValue(cc::setting::Key::StatsDbPath);
    const std::filesystem::path dbPath =
        custom.isEmpty() ? paths.userDir / cc::stats::databaseFileName : std::filesystem::path(custom.toStdWString());
    auto store = std::make_unique<cc::stats::SqliteStatsStore>();
    auto opened = store->open(dbPath);
    if (opened) {
        opened = store->beginSession("routine", cc::appVersion());
    }
    if (!opened) {
        messages.push_back(cc::ui::errorText(opened.error()));
        return nullptr;
    }
    return store;
}

/// Styles the application from the theme file; false when the file is unreadable (nothing to draw with).
bool installTheme(cc::ui::Theme &theme, const cc::ui::AppPaths &paths, const cc::ui::Settings &settings)
{
    if (auto r = theme.load(paths.theme(wantsDarkTheme(settings))); !r) {
        qCCritical(logApp) << "theme load failed:" << QString::fromStdString(r.error().detail);
        return false;
    }
    QApplication::setStyle(new cc::ui::CopyCoachStyle(theme)); // QApplication owns it
    QApplication::setPalette(theme.palette(cc::ui::Surface::Canvas));
    QApplication::setFont(theme.font(cc::theme::TypeStyle::Body));
    const bool large = settings.stringValue(cc::setting::Key::AppearanceFontSize) == QStringLiteral("large");
    theme.setFontScale(large ? 1.15 : 1.0);
    return true;
}

void saveOnExit(const cc::ui::Settings &settings,
                const cc::ui::AppPaths &paths,
                const cc::ui::RoutineSession &session,
                cc::stats::SqliteStatsStore *stats)
{
    if (auto r = settings.save(); !r) {
        qCWarning(logApp) << "settings save failed:" << QString::fromStdString(r.error().detail);
    }
    if (auto r = cc::routine::saveRoutineState(paths.userDir / "routine-state.json", session.state()); !r) {
        qCWarning(logApp) << "routine state save failed:" << QString::fromStdString(r.error().detail);
    }
    if (stats != nullptr) {
        if (auto r = stats->endSession(); !r) {
            qCWarning(logApp) << "stats session end failed:" << QString::fromStdString(r.error().detail);
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    const QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CopyCoach"));
    QCoreApplication::setApplicationName(QStringLiteral("CopyCoach"));

    const cc::ui::AppPaths paths = cc::ui::AppPaths::detect();
    std::vector<QString> messages;
    cc::ui::Settings settings(paths.userDir);
    if (auto r = settings.load(); !r) {
        messages.push_back(cc::ui::errorText(r.error()));
    }
    cc::ui::Theme theme;
    if (!installTheme(theme, paths, settings)) {
        return 1;
    }
    cc::ui::ActionRegistry actions;
    cc::ui::Keymap keymap(paths.userDir);
    if (auto r = keymap.load(); !r) {
        messages.push_back(cc::ui::errorText(r.error()));
    }
    keymap.apply(actions);
    cc::ui::RoutineSession session(loadDefinition(paths, messages), loadState(paths, settings, messages));
    cc::ui::AudioService audio(paths, settings);
    cc::ui::JobService jobs;
    cc::stats::NullStatsSink nullSink;
    const std::unique_ptr<cc::stats::SqliteStatsStore> stats = openStats(paths, settings, messages);
    cc::stats::IStatsSink *sink = stats ? static_cast<cc::stats::IStatsSink *>(stats.get()) : &nullSink;
    const cc::ui::ShellContext
        context{&paths, &theme, &settings, &actions, &audio, &session, &keymap, sink, stats.get(), &jobs};
    cc::ui::Shell shell(context);
    for (const QString &message : messages) {
        shell.showStatus(message);
    }
    shell.show();
    const int code = QApplication::exec();
    saveOnExit(settings, paths, session, stats.get());
    return code;
}
