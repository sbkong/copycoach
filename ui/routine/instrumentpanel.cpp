// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/instrumentpanel.h"

#include "ui/routine/routinesession.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace cc::ui
{

namespace
{

constexpr std::array<const char *, 8> circled{"①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧"};

QString circledNumber(int slot)
{
    return slot >= 0 && slot < 8 ? QString::fromUtf8(circled[static_cast<std::size_t>(slot)]) : QString();
}

} // namespace

InstrumentPanel::InstrumentPanel(const Theme &theme, Settings &settings, RoutineSession &session, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_settings(&settings)
    , m_session(&session)
    , m_fretboard(new FretboardView(theme, this))
    , m_keyboard(new KeyboardStrip(theme, this))
    , m_keyboardToggle(new QToolButton(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = new QHBoxLayout();
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    header->setContentsMargins(edge, theme.spacing(theme::Spacing::Unit), edge, theme.spacing(theme::Spacing::Unit));
    auto *title = new QLabel(tr("인스트루먼트 · 지판"), this);
    title->setFont(theme.font(theme::TypeStyle::BodyStrong));
    header->addWidget(title);
    header->addStretch();
    m_keyboardToggle->setText(tr("건반"));
    m_keyboardToggle->setCheckable(true);
    m_keyboardToggle->setToolTip(tr("건반 보조 스트립 켬/끔"));
    m_keyboardToggle->setAccessibleName(tr("건반 보조 스트립 켬/끔"));
    header->addWidget(m_keyboardToggle);
    layout->addLayout(header);
    layout->addWidget(m_fretboard, 1);
    layout->addWidget(m_keyboard);

    connect(m_keyboardToggle, &QToolButton::toggled, this, [this](bool on) {
        m_settings->setValue(setting::Key::ViewKeyboardStrip, nlohmann::json(on));
    });
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        applySettings();
    });
    connect(m_session, &RoutineSession::slotsChanged, this, &InstrumentPanel::refresh);
    connect(m_session, &RoutineSession::selectionChanged, this, [this](int) {
        refresh();
    });
    connect(m_session, &RoutineSession::playingSlotChanged, this, [this](int) {
        refresh();
    });
    connect(m_fretboard, &FretboardView::positionClicked, this, [this](theory::StringFret position) {
        emit noteClicked(m_session->tuning().pitchAt(position));
    });
    connect(m_keyboard, &KeyboardStrip::pitchClicked, this, &InstrumentPanel::noteClicked);
    applySettings();
    refresh();
}

FretboardView *InstrumentPanel::fretboard() const
{
    return m_fretboard;
}

KeyboardStrip *InstrumentPanel::keyboard() const
{
    return m_keyboard;
}

void InstrumentPanel::applySettings()
{
    const bool strip = m_settings->boolValue(setting::Key::ViewKeyboardStrip);
    m_keyboardToggle->setChecked(strip);
    m_keyboard->setVisible(strip);
    const bool large = m_settings->stringValue(setting::Key::AppearanceFretboardDotSize) == QStringLiteral("large");
    m_fretboard->setDotSize(large ? DotSize::Large : DotSize::Normal);
    refresh();
}

bool InstrumentPanel::showsNote(const SlotNote &note) const
{
    const QString root = m_settings->stringValue(setting::Key::ViewFretboardRoot);
    if (root == QStringLiteral("hide")) {
        return !note.isRoot;
    }
    if (root == QStringLiteral("only")) {
        return note.isRoot;
    }
    return true;
}

QString InstrumentPanel::labelFor(const SlotNote &note) const
{
    const bool names = m_settings->stringValue(setting::Key::ViewFretboardLabel) == QStringLiteral("name");
    return names ? note.noteName : note.degreeLabel;
}

QColor InstrumentPanel::fillFor(const SlotNote &note, const Slot &slot) const
{
    const bool degree = m_settings->stringValue(setting::Key::ViewFretboardColor) == QStringLiteral("degree");
    return m_theme->color(degree || note.kind == DotKind::Scale ? note.degreeColor : slot.chordColor);
}

void InstrumentPanel::addPreviewDots(FretboardScene &scene, const Slot &next) const
{
    for (const SlotNote &note : next.notes) {
        if (showsNote(note) && note.kind != DotKind::Upper) {
            scene.dots.push_back(FretDot{note.position, labelFor(note), QColor(), DotShape::Dashed, false});
        }
    }
}

FretboardScene InstrumentPanel::sceneFor(int shownSlot) const
{
    FretboardScene scene;
    const std::vector<Slot> &slotList = m_session->slotList();
    if (shownSlot < 0 || std::cmp_greater_equal(shownSlot, slotList.size())) {
        return scene;
    }
    const Slot &slot = slotList[static_cast<std::size_t>(shownSlot)];
    if (slotList.size() > 1 && !slot.isScaleRun) {
        addPreviewDots(scene, slotList[static_cast<std::size_t>((shownSlot + 1) % static_cast<int>(slotList.size()))]);
    }
    for (const SlotNote &note : slot.notes) {
        if (!showsNote(note)) {
            continue;
        }
        const DotShape shape = note.kind == DotKind::Upper ? DotShape::Diamond : DotShape::Circle;
        scene.dots.push_back(FretDot{note.position, labelFor(note), fillFor(note, slot), shape, note.isRoot});
    }
    scene.caption = slot.isScaleRun ? slot.name : circledNumber(shownSlot) + QLatin1Char(' ') + slot.name;
    if (!slot.isScaleRun && !slot.rootString.isEmpty()) {
        scene.caption += QStringLiteral(" · ") + slot.rootString;
    }
    return scene;
}

std::vector<KeyMark> InstrumentPanel::marksFor(int shownSlot) const
{
    std::vector<KeyMark> marks;
    const std::vector<Slot> &slotList = m_session->slotList();
    if (shownSlot < 0 || std::cmp_greater_equal(shownSlot, slotList.size())) {
        return marks;
    }
    const Slot &slot = slotList[static_cast<std::size_t>(shownSlot)];
    for (const SlotNote &note : slot.notes) {
        if (showsNote(note)) {
            const DotShape shape = note.kind == DotKind::Upper ? DotShape::Diamond : DotShape::Circle;
            marks.push_back(KeyMark{m_session->tuning().pitchAt(note.position), fillFor(note, slot), shape});
        }
    }
    return marks;
}

void InstrumentPanel::refresh()
{
    const int shown = m_session->playingSlot() >= 0 ? m_session->playingSlot() : m_session->selectedSlot();
    m_fretboard->setScene(sceneFor(shown));
    m_keyboard->setMarks(marksFor(shown));
}

} // namespace cc::ui
