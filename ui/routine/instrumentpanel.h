// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "ui/fretboard/fretboardview.h"
#include "ui/keyboard/keyboardstrip.h"
#include "ui/routine/slotbuilder.h"

#include <QWidget>

class QToolButton;

// The instrument panel (ia.md section 1-2): fretboard + keyboard helper strip, the routine's central widget.
// Builds the fretboard scene from the session (current / selected slot, next-slot preview) and the display
// settings (label, colour, root). Clicks on either instrument ask the shell to sound the note.
namespace cc::ui
{

class RoutineSession;
class Settings;
class Theme;

class InstrumentPanel final : public QWidget
{
    Q_OBJECT
public:
    InstrumentPanel(const Theme &theme, Settings &settings, RoutineSession &session, QWidget *parent = nullptr);

    [[nodiscard]] FretboardView *fretboard() const;
    [[nodiscard]] KeyboardStrip *keyboard() const;

signals:
    void noteClicked(int midi);

private:
    void refresh();
    void applySettings();
    [[nodiscard]] FretboardScene sceneFor(int shownSlot) const;
    void addPreviewDots(FretboardScene &scene, const Slot &next) const;
    [[nodiscard]] std::vector<KeyMark> marksFor(int shownSlot) const;
    [[nodiscard]] QColor fillFor(const SlotNote &note, const Slot &slot) const;
    [[nodiscard]] QString labelFor(const SlotNote &note) const;
    [[nodiscard]] bool showsNote(const SlotNote &note) const;

    const Theme *m_theme;
    Settings *m_settings;
    RoutineSession *m_session;
    FretboardView *m_fretboard;
    KeyboardStrip *m_keyboard;
    QToolButton *m_keyboardToggle;
};

} // namespace cc::ui
