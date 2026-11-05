// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/error.h"
#include "core/model/score.h"
#include "core/project/project.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <filesystem>
#include <optional>
#include <vector>

class QTimer;

namespace cc::ui
{

class ScoreEditor;
class Settings;

/// The open project as the shell sees it (states.md section 5, copy-spec.md section 1): which folder it lives
/// in, whether it has unsaved changes, the autosave timer, the recent list and the last-session position. The
/// editor owns the project data; this owns its life on disk. Dialogs stay in the shell. Main thread only.
class ProjectSession final : public QObject
{
    Q_OBJECT
public:
    ProjectSession(ScoreEditor &editor, Settings &settings, QObject *parent = nullptr);

    /// A new, empty project saved into `folder` (created when missing). Replaces whatever was open.
    [[nodiscard]] Result<void> create(const std::filesystem::path &folder);
    /// Opens project.json; when an autosave is newer, recoveryAvailable() says so and the saved file is shown.
    [[nodiscard]] Result<void> open(const std::filesystem::path &folder);
    /// Loads the newer autosave over the open project; it then counts as modified.
    [[nodiscard]] Result<void> recover();
    [[nodiscard]] Result<void> save(); // needs a folder: the shell asks for one first
    [[nodiscard]] Result<void> saveAs(const std::filesystem::path &folder);
    /// Replaces the score of the open project (an undoable edit); without a project, opens an unsaved one.
    void importScore(const model::Score &score);
    void close(); // forget the project without saving

    [[nodiscard]] bool hasProject() const;
    [[nodiscard]] const std::optional<std::filesystem::path> &folder() const;
    [[nodiscard]] QString title() const; // the folder name, or the empty-project name
    [[nodiscard]] bool modified() const;
    [[nodiscard]] std::size_t editCount() const; // for the loss dialog: what would be lost
    [[nodiscard]] std::optional<QDateTime> lastAutosave() const;
    [[nodiscard]] std::optional<QString> recoverableAutosave() const; // its saved_at, while an offer stands
    [[nodiscard]] std::vector<std::filesystem::path> recent() const;

    void autosaveNow(); // what the timer does; public for tests

signals:
    void projectChanged(); // opened, created, closed
    void modifiedChanged(bool modified);
    void autosaved(const QDateTime &when);
    void recoveryAvailable(const QString &savedAt);

private:
    void adopt(project::Project project, std::optional<std::filesystem::path> folder);
    void rememberRecent(const std::filesystem::path &folder);
    void restartTimer();
    void storeLastSession();
    void restoreLastSession();
    void noteModified();

    ScoreEditor *m_editor;
    Settings *m_settings;
    QTimer *m_timer;
    std::optional<std::filesystem::path> m_folder;
    bool m_lastModified = false;
    std::optional<QDateTime> m_lastAutosave;
    std::size_t m_autosavedDepth = 0; // undo depth at the last autosave: nothing new = nothing written
    std::optional<QString> m_recoverable;
};

} // namespace cc::ui
