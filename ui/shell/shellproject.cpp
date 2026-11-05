// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/score/musicxmlimport.h"
#include "generated/settingkeys.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/projectsession.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

// file.new / open / save / save-as / recent, the loss dialog (one of the three allowed), the recovery message
// above the score and the autosave chip (states.md section 5).
namespace cc::ui
{

using action::Id;

namespace
{

constexpr int autosaveChipRefreshMs = 5000;

QString folderText(const std::filesystem::path &folder)
{
    return QString::fromStdWString(folder.wstring());
}

} // namespace

void Shell::wireProjectActions()
{
    m_project = new ProjectSession(*m_editor, *m_settings, this);
    connect(m_actions->action(Id::FileNew), &QAction::triggered, this, &Shell::newProject);
    connect(m_actions->action(Id::FileOpen), &QAction::triggered, this, &Shell::openProject);
    connect(m_actions->action(Id::FileSave), &QAction::triggered, this, &Shell::saveProject);
    connect(m_actions->action(Id::FileSaveAs), &QAction::triggered, this, &Shell::saveProjectAs);
    connect(m_project, &ProjectSession::projectChanged, this, &Shell::updateProjectTitle);
    connect(m_project, &ProjectSession::modifiedChanged, this, &Shell::updateProjectTitle);
    connect(m_project, &ProjectSession::recoveryAvailable, this, &Shell::offerRecovery);
    connect(m_project, &ProjectSession::autosaved, this, &Shell::updateAutosaveChip);
    auto *tick = new QTimer(this);
    tick->setInterval(autosaveChipRefreshMs);
    connect(tick, &QTimer::timeout, this, &Shell::updateAutosaveChip);
    tick->start();
    updateProjectTitle();
}

/// "'Autumn Leaves'의 변경 사항을 저장할까요? ..." — true when the caller may go on (saved or discarded).
bool Shell::confirmLoss()
{
    if (!m_project->modified()) {
        return true;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setText(tr("'%1'의 변경 사항을 저장할까요?").arg(m_project->title()));
    box.setInformativeText(tr("저장하지 않으면 편집 %1개가 사라집니다.").arg(m_project->editCount()));
    QPushButton *save = box.addButton(tr("저장"), QMessageBox::AcceptRole);
    box.addButton(tr("저장 안 함"), QMessageBox::DestructiveRole);
    const QPushButton *cancel = box.addButton(tr("취소"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == cancel) {
        return false;
    }
    if (box.clickedButton() == save) {
        return saveProject();
    }
    return true;
}

std::filesystem::path Shell::defaultProjectDir() const
{
    const QString configured = m_settings->stringValue(setting::Key::FilesProjectDir);
    if (!configured.isEmpty()) {
        return {configured.toStdWString()};
    }
    return m_paths->userDir / "projects";
}

void Shell::newProject()
{
    if (!confirmLoss()) {
        return;
    }
    const QString chosen = QFileDialog::getSaveFileName(this,
                                                        tr("새 프로젝트 폴더"),
                                                        folderText(defaultProjectDir() / tr("새 곡").toStdWString()),
                                                        QString());
    if (chosen.isEmpty()) {
        return;
    }
    const Result<void> r = m_project->create(std::filesystem::path(chosen.toStdWString()));
    showStatus(r ? tr("새 프로젝트: %1").arg(m_project->title()) : errorText(r.error()));
    if (r) {
        switchTo(workspace::Id::Copy);
    }
}

void Shell::openProject()
{
    if (!confirmLoss()) {
        return;
    }
    const QString chosen =
        QFileDialog::getExistingDirectory(this, tr("프로젝트 폴더 열기"), folderText(defaultProjectDir()));
    if (!chosen.isEmpty()) {
        openProjectFolder(std::filesystem::path(chosen.toStdWString()));
    }
}

void Shell::openProjectFolder(const std::filesystem::path &folder)
{
    m_copyMessage->clear();
    const Result<void> r = m_project->open(folder);
    if (!r) {
        showStatus(errorText(r.error()));
        return;
    }
    m_scoreView->setScore(m_editor->score());
    switchTo(workspace::Id::Copy);
    showStatus(tr("열림: %1").arg(m_project->title()));
}

bool Shell::saveProject()
{
    if (!m_project->hasProject()) {
        return true;
    }
    if (!m_project->folder()) {
        return saveProjectAs();
    }
    const Result<void> r = m_project->save();
    showStatus(r ? tr("저장됨: %1").arg(m_project->title()) : errorText(r.error()));
    return r.has_value();
}

bool Shell::saveProjectAs()
{
    if (!m_project->hasProject()) {
        return true;
    }
    const QString chosen =
        QFileDialog::getSaveFileName(this,
                                     tr("프로젝트 폴더로 저장"),
                                     folderText(defaultProjectDir() / m_project->title().toStdWString()),
                                     QString());
    if (chosen.isEmpty()) {
        return false;
    }
    const Result<void> r = m_project->saveAs(std::filesystem::path(chosen.toStdWString()));
    showStatus(r ? tr("저장됨: %1").arg(m_project->title()) : errorText(r.error()));
    return r.has_value();
}

/// states.md section 5: "저장하지 않은 변경(10:42 자동 저장)이 있습니다" [복구] [무시], above the score.
void Shell::offerRecovery(const QString &savedAt)
{
    m_copyMessage->show(MessageKind::Notice,
                        tr("저장하지 않은 변경(%1 자동 저장)이 있습니다").arg(savedAt),
                        {MessageAction{tr("복구"),
                                       [this] {
                                           const Result<void> r = m_project->recover();
                                           if (r) {
                                               m_scoreView->setScore(m_editor->score());
                                           }
                                           showStatus(r ? tr("복구함") : errorText(r.error()));
                                           m_copyMessage->clear();
                                       }},
                         MessageAction{tr("무시"), [this] {
                                           m_copyMessage->clear();
                                       }}});
}

/// copy.speed.down / up / reset: 5 % steps on the recording (copy-spec.md section 3); the label shows the
/// result next to the editor context.
void Shell::wireCopyTransport()
{
    constexpr double step = 0.05;
    connect(m_actions->action(Id::CopySpeedDown), &QAction::triggered, this, [this] {
        m_audio->setSpeed(m_audio->speed() - step);
        updateSpeedLabel();
    });
    connect(m_actions->action(Id::CopySpeedUp), &QAction::triggered, this, [this] {
        m_audio->setSpeed(m_audio->speed() + step);
        updateSpeedLabel();
    });
    connect(m_actions->action(Id::CopySpeedReset), &QAction::triggered, this, [this] {
        m_audio->setSpeed(1.0);
        updateSpeedLabel();
    });
    connect(m_project, &ProjectSession::sourcesChanged, this, [this] {
        if (m_active == workspace::Id::Copy) {
            pushCopyScore();
        }
    });
}

void Shell::updateSpeedLabel()
{
    constexpr double percent = 100.0;
    if (!m_audio->copyMode()) {
        return;
    }
    showStatus(tr("속도 %1 %").arg(qRound(m_audio->speed() * percent)));
}

void Shell::updateProjectTitle()
{
    const QString title = m_project->title();
    const QString mark = m_project->modified() ? QStringLiteral("*") : QString();
    setWindowTitle(title.isEmpty() ? QStringLiteral("CopyCoach") : QStringLiteral("%1%2 - CopyCoach").arg(title, mark));
    rebuildRecentMenu();
    updateAutosaveChip();
}

void Shell::updateAutosaveChip()
{
    const auto when = m_project->lastAutosave();
    if (!when) {
        m_autosaveLabel->clear();
        return;
    }
    const qint64 seconds = when->secsTo(QDateTime::currentDateTime());
    m_autosaveLabel->setText(tr("자동 저장 %1초 전").arg(seconds));
}

void Shell::openRecent(const std::filesystem::path &folder)
{
    if (confirmLoss()) {
        openProjectFolder(folder);
    }
}

void Shell::rebuildRecentMenu()
{
    m_recentMenu->clear();
    for (const std::filesystem::path &folder : m_project->recent()) {
        QAction *entry = m_recentMenu->addAction(QString::fromStdWString(folder.filename().wstring()));
        entry->setToolTip(folderText(folder));
        connect(entry, &QAction::triggered, this, [this, folder] {
            openRecent(folder);
        });
    }
    m_recentMenu->setEnabled(!m_recentMenu->isEmpty());
}

} // namespace cc::ui
