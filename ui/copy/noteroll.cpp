// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/noteroll.h"

#include "ui/theme/theme.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace cc::ui
{

namespace
{

constexpr int preferredHeight = 120;
constexpr int marginMidi = 2; // keep a little air above and below the notes
constexpr int playheadWidthPx = 2;
constexpr int minBarWidthPx = 2;

} // namespace

NoteRollView::NoteRollView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
{
    setAccessibleName(tr("예측 음"));
    setMinimumHeight(preferredHeight);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, m_theme->color(theme::Token::BackgroundCanvasColor));
    setPalette(pal);
}

void NoteRollView::setNotes(std::vector<predict::NoteEvent> notes, double seconds)
{
    m_notes = std::move(notes);
    m_seconds = seconds;
    if (!m_notes.empty()) {
        const auto [low, high] = std::ranges::minmax_element(m_notes, {}, &predict::NoteEvent::midi);
        m_lowMidi = low->midi - marginMidi;
        m_highMidi = high->midi + marginMidi;
    }
    update();
}

void NoteRollView::clear()
{
    m_notes.clear();
    m_seconds = 0.0;
    update();
}

void NoteRollView::setPlayheadSeconds(std::optional<double> seconds)
{
    if (m_playhead != seconds) {
        m_playhead = seconds;
        update();
    }
}

const std::vector<predict::NoteEvent> &NoteRollView::notes() const
{
    return m_notes;
}

QSize NoteRollView::sizeHint() const
{
    return {0, preferredHeight};
}

int NoteRollView::xAt(double seconds) const
{
    return m_seconds > 0.0 ? static_cast<int>(std::lround(seconds / m_seconds * width())) : 0;
}

int NoteRollView::yAt(int midi) const
{
    const int span = std::max(1, m_highMidi - m_lowMidi);
    return static_cast<int>(std::lround(static_cast<double>(m_highMidi - midi) / span * height()));
}

/// One bar per note from its start to its end, a row per semitone.
void NoteRollView::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    if (m_notes.empty()) {
        painter.setPen(m_theme->color(theme::Token::ContentColorSecondary));
        painter.setFont(m_theme->font(theme::TypeStyle::Body));
        painter.drawText(rect(), Qt::AlignCenter, tr("예측한 음이 없습니다 (카피 › 음 예측 실행…)"));
        return;
    }
    const int rowHeight = std::max(1, yAt(m_lowMidi) - yAt(m_lowMidi + 1));
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_theme->color(theme::Token::PredictionColor));
    for (const predict::NoteEvent &note : m_notes) {
        const int x0 = xAt(note.startSeconds);
        const int x1 = std::max(x0 + minBarWidthPx, xAt(note.endSeconds));
        painter.drawRect(x0, yAt(note.midi) - rowHeight, x1 - x0, rowHeight);
    }
    if (m_playhead) {
        painter.setBrush(m_theme->color(theme::Token::PlayheadColor));
        painter.drawRect(xAt(*m_playhead), 0, playheadWidthPx, height());
    }
}

} // namespace cc::ui
