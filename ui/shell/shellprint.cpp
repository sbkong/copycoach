// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "generated/tokens.h"
#include "ui/score/scoredocument.h"
#include "ui/score/scoreview.h"
#include "ui/shell/actionregistry.h"
#include "ui/theme/theme.h"

#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>

#include <algorithm>

// file.print (ia.md section 2 "파일"): the copy score through the system print dialog (a PDF printer gives the
// stage-6 "차트 PDF" for free). Pages are engraved again in black ink at a fixed width, whatever the theme on
// screen, then scaled to the printer's page.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr int printPageWidthPx = 1400; // engraving width; the page scales to the paper

} // namespace

void Shell::wirePrintActions()
{
    connect(m_actions->action(Id::FilePrint), &QAction::triggered, this, &Shell::printScore);
}

void Shell::printScore()
{
    if (m_active != workspace::Id::Copy || !m_scoreView->hasScore()) {
        showStatus(tr("인쇄할 악보가 없습니다 (카피 작업공간에서 악보를 열거나 만드세요)"));
        return;
    }
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("인쇄"));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    printScoreTo(printer);
}

/// Every page of a fresh black-ink engraving, scaled to fit the printable area, one printer page each.
void Shell::printScoreTo(QPrinter &printer)
{
    ScoreDocument document(m_paths->verovioDir());
    document.setInk(Qt::black, m_theme->fontFamilies(theme::FontKey::FontFamilyScoreText).value(0));
    document.setPageWidth(printPageWidthPx);
    if (!document.setScore(m_scoreView->score())) {
        showStatus(tr("악보를 조판하지 못해 인쇄할 수 없습니다"));
        return;
    }
    QPainter painter(&printer);
    if (!painter.isActive()) {
        showStatus(tr("프린터를 열지 못했습니다"));
        return;
    }
    const QRectF area = printer.pageRect(QPrinter::DevicePixel);
    for (int i = 0; i < document.pageCount(); ++i) {
        if (i > 0) {
            printer.newPage();
        }
        const ScoreDocument::Page &page = document.page(i);
        const double scale = std::min(area.width() / page.sizePx.width(), area.height() / page.sizePx.height());
        painter.save();
        painter.translate(area.topLeft());
        painter.scale(scale, scale);
        painter.drawPicture(0, 0, page.picture);
        painter.restore();
    }
    showStatus(tr("%1쪽을 인쇄했습니다").arg(document.pageCount()));
}

} // namespace cc::ui
