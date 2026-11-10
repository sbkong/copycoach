// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/stats/reader.h"
#include "core/theory/weekdayplan.h"
#include "core/version.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/prefs/preferencesdialog.h"
#include "ui/routine/routinesession.h"
#include "ui/score/scoreeditor.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/commandpalette.h"
#include "ui/shell/errortext.h"

#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QUrl>

#include <string>

// The small window-level actions (ia.md section 2 menus): full screen, select all / go to measure in the
// copy score, the help menu (manual = the repository README, shortcuts = the preferences page, about = an
// inline line with the licence folder), the stats export (the file dialog, one of the three allowed kinds)
// and "오늘 루틴 다시 배정" (the weekday plan's mode and keys back onto the routine session).
namespace cc::ui
{

using action::Id;

namespace
{

constexpr const char *manualUrl = "https://github.com/sbkong/copycoach#readme";

QString fromView(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

void Shell::wireMiscActions()
{
    m_actions->action(Id::ViewFullscreen)->setCheckable(true);
    connect(m_actions->action(Id::ViewFullscreen), &QAction::toggled, this, [this](bool on) {
        if (on) {
            showFullScreen();
        } else {
            showNormal();
        }
    });
    connect(m_actions->action(Id::EditSelectAll), &QAction::triggered, this, &Shell::selectAllMeasures);
    connect(m_actions->action(Id::EditGoTo), &QAction::triggered, this, [this] {
        m_palette->openWithQuery(tr("마디 "));
    });
    connect(m_palette, &CommandPalette::goToMeasureRequested, this, &Shell::goToMeasure);
    connect(m_actions->action(Id::HelpManual), &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl(QString::fromUtf8(manualUrl)));
    });
    connect(m_actions->action(Id::HelpShortcuts), &QAction::triggered, this, [this] {
        showPreferences(static_cast<int>(PreferencesPage::Shortcuts));
    });
    connect(m_actions->action(Id::HelpAbout), &QAction::triggered, this, &Shell::showAbout);
    connect(m_actions->action(Id::FileExportStats), &QAction::triggered, this, &Shell::exportStats);
    connect(m_actions->action(Id::RoutineTodayReassign), &QAction::triggered, this, &Shell::reassignToday);
}

void Shell::selectAllMeasures()
{
    if (m_active != workspace::Id::Copy || !m_scoreView->hasScore() || m_scoreView->score().measures.empty()) {
        return;
    }
    m_scoreView->setSelectedMeasures(std::pair{std::size_t{0}, m_scoreView->score().measures.size() - 1});
}

/// edit.go-to: the measure becomes the cursor's and the selection (1-based from the palette).
void Shell::goToMeasure(int measure)
{
    if (m_active != workspace::Id::Copy || !m_scoreView->hasScore() || measure < 1) {
        return;
    }
    const auto index = static_cast<std::size_t>(measure - 1);
    if (index >= m_scoreView->score().measures.size()) {
        showStatus(tr("마디 %1이(가) 없습니다 (마디 %2개)").arg(measure).arg(m_scoreView->score().measures.size()));
        return;
    }
    m_editor->setCursor(BeatLocation{index, 0, 1, 0});
    m_scoreView->setSelectedMeasures(std::pair{index, index});
    showStatus(tr("마디 %1").arg(measure));
}

/// help.about: version and licences in one inline line; the folder next to the executable holds the
/// third-party texts the build copies there.
void Shell::showAbout()
{
    const QString text = tr("CopyCoach %1 · 자체 코드 MIT · 서드파티 라이선스는 실행 파일 옆 licenses/ 폴더")
                             .arg(fromView(appVersion()));
    m_shellMessage->show(MessageKind::Info,
                         text,
                         {MessageAction{tr("라이선스 폴더 열기"),
                                        [] {
                                            QDesktopServices::openUrl(QUrl::fromLocalFile(
                                                QCoreApplication::applicationDirPath() + QStringLiteral("/licenses")));
                                        }},
                          MessageAction{tr("닫기"), [this] {
                                            m_shellMessage->clear();
                                        }}});
}

/// file.export-stats: every event of stats.db as CSV (stats.md section 7).
void Shell::exportStats()
{
    if (m_statsReader == nullptr) {
        showStatus(tr("기록이 꺼져 있어 내보낼 것이 없습니다"));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this,
                                                      tr("기록 내보내기"),
                                                      QDir::homePath() + QStringLiteral("/copycoach-stats.csv"),
                                                      tr("CSV (*.csv)"));
    if (file.isEmpty()) {
        return;
    }
    exportStatsTo(std::filesystem::path(file.toStdWString()));
}

void Shell::exportStatsTo(const std::filesystem::path &path)
{
    if (const Result<void> r = m_statsReader->exportCsv(path); !r) {
        showStatus(errorText(r.error()));
        return;
    }
    showStatus(tr("기록을 내보냈습니다: %1").arg(QString::fromStdWString(path.wstring())));
}

/// routine.today.reassign: today's weekday plan (routine-spec.md section 6) back onto the session, after the
/// user changed keys or mode by hand. A review day only resets the keys.
void Shell::reassignToday()
{
    const int qtDay = QDate::currentDate().dayOfWeek(); // 1 = Monday .. 7 = Sunday
    const auto weekday = static_cast<theory::Weekday>(qtDay % 7);
    const theory::WeekdayPlan &plan = m_session->definition().weekdayPlan[static_cast<std::size_t>(weekday)];
    m_session->setKeys({plan.keys[0], plan.keys[1]});
    if (!plan.review && m_session->module() != nullptr && m_session->module()->id == "01") {
        m_session->setOption("mode", std::to_string(plan.mode));
    }
    const QString keys =
        fromView(theory::flatName(plan.keys[0])) + QStringLiteral(" · ") + fromView(theory::flatName(plan.keys[1]));
    showStatus(plan.review ? tr("오늘은 복습: 키 %1 (선법은 그대로)").arg(keys)
                           : tr("오늘 루틴 다시 배정: 선법 %1 · 키 %2").arg(plan.mode).arg(keys));
}

} // namespace cc::ui
