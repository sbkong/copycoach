// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/model/score.h"
#include "ui/score/overlays.h"

#include <QWidget>

#include <optional>

class QTabWidget;

namespace cc::ui
{

class AudioTrackView;
class FretboardView;
class Theme;

/// The copy workspace's bottom dock (copy-spec.md section 16.2 C-4, 1280x660 budget): one 120 px slot with
/// the audio track and the fretboard as tabs, the audio track in front. The fretboard shows the notes of
/// the beat under the score cursor.
class CopyBottomDock final : public QWidget
{
    Q_OBJECT
public:
    explicit CopyBottomDock(const Theme &theme, QWidget *parent = nullptr);

    [[nodiscard]] AudioTrackView *audioTrack() const;
    [[nodiscard]] FretboardView *fretboard() const;

    /// Dots for the notes of the beat at `cursor` (empty when none), labelled with their frets.
    void showBeat(const model::Score &score, std::optional<BeatLocation> cursor);
    void showAudioTrack();

private:
    const Theme *m_theme;
    QTabWidget *m_tabs;
    AudioTrackView *m_audioTrack;
    FretboardView *m_fretboard;
};

} // namespace cc::ui
