// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/predict/basicpitch.h"

#include <QWidget>

#include <optional>
#include <vector>

namespace cc::ui
{

class Theme;

/// Predicted notes as a piano roll in the prediction colour (copy-spec.md section 4 "음", C-6): time across
/// the whole recording, pitch up, one bar per note; the playhead as in the audio track. A view of the grey
/// layer only: notes enter the score through copy.predict.accept.
class NoteRollView final : public QWidget
{
    Q_OBJECT
public:
    explicit NoteRollView(const Theme &theme, QWidget *parent = nullptr);

    void setNotes(std::vector<predict::NoteEvent> notes, double seconds);
    void clear();
    void setPlayheadSeconds(std::optional<double> seconds);
    [[nodiscard]] const std::vector<predict::NoteEvent> &notes() const;
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    [[nodiscard]] int xAt(double seconds) const;
    [[nodiscard]] int yAt(int midi) const;

    const Theme *m_theme;
    std::vector<predict::NoteEvent> m_notes;
    double m_seconds = 0.0;
    int m_lowMidi = 40; // E2
    int m_highMidi = 88; // E6
    std::optional<double> m_playhead;
};

} // namespace cc::ui
