// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/copy/copybottomdock.h"

#include "ui/copy/audiotrackview.h"
#include "ui/fretboard/fretboardview.h"
#include "ui/theme/theme.h"

#include <QTabWidget>
#include <QVBoxLayout>

namespace cc::ui
{

CopyBottomDock::CopyBottomDock(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_tabs(new QTabWidget(this))
    , m_audioTrack(new AudioTrackView(theme, this))
    , m_fretboard(new FretboardView(theme, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabPosition(QTabWidget::South);
    m_tabs->addTab(m_audioTrack, tr("오디오 트랙"));
    m_tabs->addTab(m_fretboard, tr("지판"));
    layout->addWidget(m_tabs);
}

AudioTrackView *CopyBottomDock::audioTrack() const
{
    return m_audioTrack;
}

FretboardView *CopyBottomDock::fretboard() const
{
    return m_fretboard;
}

namespace
{

const model::Beat *beatAt(const model::Score &score, const BeatLocation &at)
{
    for (const model::Beat &beat : score.measures[at.measure].beats) {
        if (beat.track == at.track && beat.voice == at.voice && beat.tick == at.tick) {
            return &beat;
        }
    }
    return nullptr;
}

} // namespace

void CopyBottomDock::showBeat(const model::Score &score, std::optional<BeatLocation> cursor)
{
    FretboardScene scene;
    const bool valid = cursor && cursor->measure < score.measures.size() && cursor->track < score.tracks.size();
    const model::Beat *beat = valid ? beatAt(score, *cursor) : nullptr;
    if (valid) {
        m_fretboard->setStringCount(score.tracks[cursor->track].tuning.stringCount());
        scene.caption = tr("마디 %1").arg(cursor->measure + 1);
    }
    for (const model::Note &note : beat != nullptr ? beat->notes : std::vector<model::Note>{}) {
        scene.dots.push_back(FretDot{{note.string, note.fret},
                                     QString::number(note.fret),
                                     m_theme->color(theme::Token::AccentColorDefault),
                                     DotShape::Circle,
                                     false});
    }
    m_fretboard->setScene(std::move(scene));
}

void CopyBottomDock::showAudioTrack()
{
    m_tabs->setCurrentWidget(m_audioTrack);
}

} // namespace cc::ui
