// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include <QAbstractButton>
#include <QWidget>

#include <vector>

class QHBoxLayout;
class QToolButton;

// Chord strip (components.md section 2.3): Arp / Chord, Mute All, Clear Solo, then one chip per slot with its
// colour bar, M / S toggles, name and roman numeral. Chip states: current (accent outline), selected, next
// (dotted), muted (dimmed text), solo on.
namespace cc::ui
{

class ActionRegistry;
class RoutineSession;
class Theme;

class ChordChip final : public QAbstractButton
{
    Q_OBJECT
public:
    ChordChip(const Theme &theme, int slot, QWidget *parent = nullptr);

    void setSlotState(const QString &title, const QString &detail, const QColor &bar, bool muted, bool solo);
    void setPlaybackState(bool current, bool selected, bool next);
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void muteToggled(int slot);
    void soloToggled(int slot);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    [[nodiscard]] QRect muteRect() const;
    [[nodiscard]] QRect soloRect() const;
    void paintToggle(QPainter &painter, const QRect &rect, const QString &text, bool on) const;

    const Theme *m_theme;
    int m_slot;
    QString m_title;
    QString m_detail;
    QColor m_bar;
    bool m_muted = false;
    bool m_solo = false;
    bool m_current = false;
    bool m_selected = false;
    bool m_next = false;
};

class ChordStrip final : public QWidget
{
    Q_OBJECT
public:
    ChordStrip(const Theme &theme, ActionRegistry &actions, RoutineSession &session, QWidget *parent = nullptr);

private:
    void rebuildChips();
    void refreshStates();

    const Theme *m_theme;
    RoutineSession *m_session;
    QToolButton *m_arp;
    QToolButton *m_chord;
    QHBoxLayout *m_chipRow;
    std::vector<ChordChip *> m_chips;
};

} // namespace cc::ui
