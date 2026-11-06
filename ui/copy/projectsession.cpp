// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/projectsession.h"

#include "core/edit/commands.h"
#include "core/model/locate.h"
#include "core/project/projectfile.h"
#include "generated/settingkeys.h"
#include "ui/score/scoreeditor.h"
#include "ui/shell/settings.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTimer>

#include <algorithm>
#include <string>

namespace cc::ui
{

namespace
{

constexpr int msPerSecond = 1000;
constexpr int minIntervalS = 1; // the settings page keeps real values sane; tests run at one second

std::filesystem::path pathOf(const nlohmann::json &value)
{
    return {value.get<std::string>()};
}

} // namespace

ProjectSession::ProjectSession(ScoreEditor &editor, Settings &settings, QObject *parent)
    : QObject(parent)
    , m_editor(&editor)
    , m_settings(&settings)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &ProjectSession::autosaveNow);
    connect(m_editor, &ScoreEditor::scoreChanged, this, &ProjectSession::noteModified);
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key key) {
        if (key == setting::Key::FilesAutosaveIntervalS) {
            restartTimer();
        }
    });
}

void ProjectSession::restartTimer()
{
    const int seconds = std::max(minIntervalS, m_settings->intValue(setting::Key::FilesAutosaveIntervalS));
    m_timer->start(seconds * msPerSecond);
}

void ProjectSession::adopt(project::Project project, std::optional<std::filesystem::path> folder)
{
    m_folder = std::move(folder);
    m_recoverable.reset();
    m_lastAutosave.reset();
    m_autosavedDepth = 0;
    m_editor->setProject(std::move(project));
    restoreLastSession();
    m_lastModified = false;
    restartTimer();
    emit projectChanged();
    emit modifiedChanged(false);
}

Result<void> ProjectSession::create(const std::filesystem::path &folder)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::WriteFailed, folder.string(), ec.message());
    }
    project::Project project = project::newProject();
    if (Result<void> r = project::saveProject(folder, project); !r) {
        return r;
    }
    adopt(std::move(project), folder);
    rememberRecent(folder);
    return {};
}

Result<void> ProjectSession::open(const std::filesystem::path &folder)
{
    Result<project::Project> loaded = project::loadProject(folder);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    adopt(std::move(*loaded), folder);
    rememberRecent(folder);
    if (project::autosaveIsNewer(folder)) {
        if (const Result<project::Project> autosave = project::loadAutosave(folder)) {
            m_recoverable = QString::fromStdString(autosave->lastSession.savedAt);
            emit recoveryAvailable(*m_recoverable);
        }
    }
    return {};
}

Result<void> ProjectSession::recover()
{
    const std::optional<std::filesystem::path> folder = m_folder;
    if (!folder || !m_recoverable) {
        return fail(ErrorCode::FileNotFound, std::string(project::autosaveFileName), "no recovery offered");
    }
    Result<project::Project> autosave = project::loadAutosave(*folder);
    if (!autosave) {
        return std::unexpected(autosave.error());
    }
    adopt(std::move(*autosave), folder);
    m_editor->undoStack().markUnsaved(); // recovered = not yet in project.json
    noteModified();
    return {};
}

Result<void> ProjectSession::save()
{
    const std::optional<std::filesystem::path> folder = m_folder;
    if (!folder) {
        return fail(ErrorCode::FileNotFound, {}, "the project has no folder yet");
    }
    storeLastSession();
    if (Result<void> r = project::saveProject(*folder, m_editor->project()); !r) {
        return r;
    }
    m_editor->undoStack().markSaved();
    m_recoverable.reset();
    m_autosavedDepth = m_editor->undoStack().undoDepth();
    noteModified();
    return {};
}

Result<void> ProjectSession::saveAs(const std::filesystem::path &folder)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        return fail(ErrorCode::WriteFailed, folder.string(), ec.message());
    }
    m_folder = folder;
    if (Result<void> r = save(); !r) {
        return r;
    }
    rememberRecent(folder);
    emit projectChanged();
    return {};
}

void ProjectSession::importScore(const model::Score &score)
{
    if (!hasProject()) {
        adopt(project::newProject(), std::nullopt);
    }
    m_editor->replaceScore(score);
}

namespace
{

/// SHA-256 of the file as lowercase hex (project.json `hash`); empty when the file cannot be read.
std::string fileHash(const std::filesystem::path &path)
{
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result().toHex().toStdString();
}

} // namespace

Result<model::Id> ProjectSession::importAudio(const std::filesystem::path &file)
{
    return importMedia(file, project::SourceKind::Audio);
}

Result<model::Id> ProjectSession::importVideo(const std::filesystem::path &file)
{
    return importMedia(file, project::SourceKind::Video);
}

Result<model::Id> ProjectSession::importMedia(const std::filesystem::path &file, project::SourceKind kind)
{
    if (!std::filesystem::exists(file)) {
        return fail(ErrorCode::FileNotFound, file.string());
    }
    if (!hasProject()) {
        adopt(project::newProject(), std::nullopt);
    }
    std::filesystem::path stored = file;
    std::string recorded = file.string();
    if (m_folder && m_settings->boolValue(setting::Key::FilesCopyMediaIntoProject)) {
        const std::filesystem::path mediaDir = *m_folder / "media";
        std::error_code ec;
        std::filesystem::create_directories(mediaDir, ec);
        stored = mediaDir / file.filename();
        if (!ec && !std::filesystem::equivalent(file, stored, ec)) {
            std::filesystem::copy_file(file, stored, std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            return fail(ErrorCode::WriteFailed, stored.string(), ec.message());
        }
        recorded = (std::filesystem::path("media") / file.filename()).generic_string();
    }
    project::Source source{m_ids.next(), kind, recorded, fileHash(stored), 0, file.stem().string()};
    model::Id id = source.id;
    if (!m_editor->runCommand(edit::addSource(m_editor->project(), std::move(source)))) {
        return fail(ErrorCode::Unsupported, recorded, "source not added");
    }
    m_editor->setActiveSource(id);
    emit sourcesChanged();
    return id;
}

std::optional<model::Id> ProjectSession::activeSourceId() const
{
    if (!hasProject()) {
        return std::nullopt;
    }
    return m_editor->project().lastSession.sourceId;
}

std::optional<project::Source> ProjectSession::activeSource() const
{
    const auto id = activeSourceId();
    if (!id) {
        return std::nullopt;
    }
    for (const project::Source &source : m_editor->project().sources) {
        if (source.id == *id) {
            return source;
        }
    }
    return std::nullopt;
}

std::filesystem::path ProjectSession::resolve(const std::string &stored) const
{
    const std::filesystem::path path(stored);
    return path.is_absolute() || !m_folder ? path : *m_folder / path;
}

std::optional<std::filesystem::path> ProjectSession::activeSourcePath() const
{
    const auto source = activeSource();
    if (!source) {
        return std::nullopt;
    }
    return source->kind == project::SourceKind::Video ? extractedAudioPath(source->id) : resolve(source->path);
}

std::optional<std::filesystem::path> ProjectSession::activeVideoPath() const
{
    const auto source = activeSource();
    if (!source || source->kind != project::SourceKind::Video) {
        return std::nullopt;
    }
    return resolve(source->path);
}

std::filesystem::path ProjectSession::extractedAudioPath(const model::Id &sourceId) const
{
    const std::filesystem::path dir =
        m_folder ? *m_folder / "cache" : std::filesystem::temp_directory_path() / "copycoach-cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / (sourceId + ".flac");
}

void ProjectSession::close()
{
    m_folder.reset();
    m_recoverable.reset();
    m_lastAutosave.reset();
    m_timer->stop();
    m_editor->clearProject();
    m_lastModified = false;
    emit projectChanged();
    emit modifiedChanged(false);
}

bool ProjectSession::hasProject() const
{
    return m_editor->hasProject();
}

const project::Project &ProjectSession::project() const
{
    return m_editor->project();
}

const std::optional<std::filesystem::path> &ProjectSession::folder() const
{
    return m_folder;
}

QString ProjectSession::title() const
{
    if (m_folder) {
        return QString::fromStdWString(m_folder->filename().wstring());
    }
    return hasProject() ? tr("저장되지 않은 프로젝트") : QString();
}

bool ProjectSession::modified() const
{
    return hasProject() && m_editor->undoStack().modified();
}

std::size_t ProjectSession::editCount() const
{
    return m_editor->undoStack().undoDepth();
}

std::optional<QDateTime> ProjectSession::lastAutosave() const
{
    return m_lastAutosave;
}

std::optional<QString> ProjectSession::recoverableAutosave() const
{
    return m_recoverable;
}

/// Writes project.autosave.json when there is something new since the last save or autosave.
void ProjectSession::autosaveNow()
{
    const std::optional<std::filesystem::path> folder = m_folder;
    if (!folder || !modified() || m_editor->undoStack().undoDepth() == m_autosavedDepth) {
        return;
    }
    storeLastSession();
    if (project::saveAutosave(*folder, m_editor->project())) {
        m_autosavedDepth = m_editor->undoStack().undoDepth();
        m_lastAutosave = QDateTime::currentDateTime();
        emit autosaved(*m_lastAutosave);
    }
}

std::vector<std::filesystem::path> ProjectSession::recent() const
{
    std::vector<std::filesystem::path> folders;
    const nlohmann::json list = m_settings->value(setting::Key::FilesRecent);
    for (const nlohmann::json &entry : list) {
        if (entry.is_string()) {
            folders.push_back(pathOf(entry));
        }
    }
    return folders;
}

/// Most recent first, no duplicates, capped by files.recent-count.
void ProjectSession::rememberRecent(const std::filesystem::path &folder)
{
    std::vector<std::filesystem::path> folders = recent();
    std::erase(folders, folder);
    folders.insert(folders.begin(), folder);
    const auto cap = static_cast<std::size_t>(std::max(1, m_settings->intValue(setting::Key::FilesRecentCount)));
    if (folders.size() > cap) {
        folders.resize(cap);
    }
    nlohmann::json list = nlohmann::json::array();
    for (const std::filesystem::path &each : folders) {
        list.push_back(each.string());
    }
    m_settings->setValue(setting::Key::FilesRecent, list);
}

void ProjectSession::storeLastSession()
{
    m_editor->storeLastSession();
}

void ProjectSession::restoreLastSession()
{
    m_editor->restoreLastSession();
}

void ProjectSession::noteModified()
{
    const bool now = modified();
    if (now != m_lastModified) {
        m_lastModified = now;
        emit modifiedChanged(now);
    }
}

} // namespace cc::ui
