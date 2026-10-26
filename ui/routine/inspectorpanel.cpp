// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/routine/inspectorpanel.h"

#include "core/theory/pitch.h"
#include "ui/routine/routinesession.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace cc::ui
{

namespace
{

constexpr int keysPerRow = 6;
constexpr std::array<int, 12> circleOfFourths{0, 5, 10, 3, 8, 1, 6, 11, 4, 9, 2, 7}; // C F Bb Eb Ab Db Gb B E A D G

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QLabel *groupLabel(const QString &text, const Theme &theme, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme.font(theme::TypeStyle::Caption));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, theme.color(theme::Token::ContentColorSecondary));
    label->setPalette(palette);
    return label;
}

QToolButton *segmentButton(const QString &label, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(label);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setAccessibleName(label);
    return button;
}

/// Option value labels: the data file keeps machine values; the inspector shows them as written in the spec.
QString optionLabel(const std::string &name, const std::string &value)
{
    static const std::map<std::string, QString> labels{
        {"circle5", QStringLiteral("Circle of 5ths")},
        {"scale", QStringLiteral("Scale Order")},
        {"off", QStringLiteral("끔")},
        {"on", QStringLiteral("켬")},
        {"show", QStringLiteral("표시")},
        {"hide", QStringLiteral("숨김")},
        {"only", QStringLiteral("루트만")},
        {"auto", QStringLiteral("자동")},
        {"root", QStringLiteral("기본")},
        {"1st", QStringLiteral("1전위")},
        {"2nd", QStringLiteral("2전위")},
        {"up", QStringLiteral("↑")},
        {"down", QStringLiteral("↓")},
    };
    const auto it = labels.find(value);
    if (it != labels.end()) {
        return it->second;
    }
    if (name == "mode") {
        return QStringLiteral("선법 ") + QString::fromStdString(value);
    }
    return QString::fromStdString(value);
}

QString optionTitle(const std::string &name)
{
    static const std::map<std::string, QString> titles{
        {"order", QStringLiteral("순서")},
        {"upper", QStringLiteral("Upper Structure")},
        {"root", QStringLiteral("루트")},
        {"root_on", QStringLiteral("Root On")},
        {"mode", QStringLiteral("선법")},
        {"strings", QStringLiteral("Strings")},
        {"inversion", QStringLiteral("Inversion")},
        {"cycle", QStringLiteral("Cycle")},
        {"direction", QStringLiteral("방향")},
    };
    const auto it = titles.find(name);
    return it == titles.end() ? QString::fromStdString(name) : it->second;
}

} // namespace

InspectorPanel::InspectorPanel(const Theme &theme, Settings &settings, RoutineSession &session, QWidget *parent)
    : QWidget(parent)
    , m_theme(&theme)
    , m_settings(&settings)
    , m_session(&session)
    , m_layout(new QVBoxLayout(this))
    , m_optionsHost(new QWidget(this))
    , m_chordTitle(new QLabel(this))
    , m_chordForm(new QFormLayout())
{
    const int edge = theme.spacing(theme::Spacing::ContentArea);
    m_layout->setContentsMargins(edge, edge, edge, edge);
    m_layout->setSpacing(theme.spacing(theme::Spacing::Control));
    buildDisplayGroup();
    auto *optionsLayout = new QVBoxLayout(m_optionsHost);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_optionsHost);
    buildKeyGrid();
    m_chordTitle->setFont(theme.font(theme::TypeStyle::Caption));
    m_layout->addWidget(m_chordTitle);
    m_layout->addLayout(m_chordForm);
    m_layout->addStretch();
    buildOptionGroups();
    syncDisplay();
    syncKeys();
    syncSelectedChord();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncDisplay();
    });
    connect(m_session, &RoutineSession::slotsChanged, this, [this] {
        buildOptionGroups();
        syncKeys();
        syncSelectedChord();
    });
    connect(m_session, &RoutineSession::selectionChanged, this, [this](int) {
        syncSelectedChord();
    });
}

InspectorPanel::Segment InspectorPanel::addSegment(QVBoxLayout *into,
                                                   const QString &title,
                                                   const std::vector<std::string> &values,
                                                   const std::vector<QString> &labels)
{
    Segment segment;
    segment.values = values;
    auto *row = new QHBoxLayout();
    row->addWidget(groupLabel(title, *m_theme, this));
    for (std::size_t i = 0; i < values.size(); ++i) {
        QToolButton *button = segmentButton(labels[i], this);
        row->addWidget(button);
        segment.buttons.push_back(button);
    }
    row->addStretch();
    into->addLayout(row);
    return segment;
}

void InspectorPanel::buildDisplaySegment(setting::Key key, const QString &title)
{
    static const std::map<std::string, QString> labels{{"degree", tr("도수")},
                                                       {"name", tr("음이름")},
                                                       {"chord", tr("코드")},
                                                       {"show", tr("표시")},
                                                       {"hide", tr("숨김")},
                                                       {"only", tr("루트만")}};
    const auto allowed = nlohmann::json::parse(setting::info(key).valuesJson, nullptr, false);
    std::vector<std::string> values;
    std::vector<QString> texts;
    for (const auto &v : allowed) {
        values.push_back(v.get<std::string>());
        texts.push_back(labels.at(values.back()));
    }
    Segment segment = addSegment(m_layout, title, values, texts);
    for (std::size_t i = 0; i < segment.buttons.size(); ++i) {
        connect(segment.buttons[i], &QToolButton::clicked, this, [this, key, value = values[i]] {
            m_settings->setValue(key, nlohmann::json(value));
        });
    }
    m_display[key] = std::move(segment);
}

void InspectorPanel::buildDisplayGroup()
{
    m_layout->addWidget(groupLabel(tr("지판 표시"), *m_theme, this));
    buildDisplaySegment(setting::Key::ViewFretboardLabel, tr("라벨"));
    buildDisplaySegment(setting::Key::ViewFretboardColor, tr("색"));
    buildDisplaySegment(setting::Key::ViewFretboardRoot, tr("루트"));
}

void InspectorPanel::syncDisplay()
{
    for (auto &[key, segment] : m_display) {
        const std::string current = m_settings->value(key).get<std::string>();
        for (std::size_t i = 0; i < segment.buttons.size(); ++i) {
            segment.buttons[i]->setChecked(segment.values[i] == current);
        }
    }
}

namespace
{

void clearRow(QLayout *row)
{
    while (const QLayoutItem *child = row->takeAt(0)) {
        delete child->widget();
        delete child;
    }
}

} // namespace

void InspectorPanel::clearOptionGroups()
{
    QLayout *host = m_optionsHost->layout();
    while (QLayoutItem *item = host->takeAt(0)) {
        if (QLayout *row = item->layout()) {
            clearRow(row);
        }
        delete item;
    }
    m_options.clear();
}

void InspectorPanel::buildOptionGroups()
{
    clearOptionGroups();
    const routine::ModeDefinition *mode = m_session->mode();
    if (mode == nullptr) {
        return;
    }
    for (const auto &[name, values] : mode->options) {
        addOptionSegment(name, values);
    }
    syncOptions();
}

void InspectorPanel::addOptionSegment(const std::string &name, const std::vector<std::string> &values)
{
    auto *host = qobject_cast<QVBoxLayout *>(m_optionsHost->layout());
    std::vector<QString> labels;
    labels.reserve(values.size());
    for (const std::string &value : values) {
        labels.push_back(optionLabel(name, value));
    }
    Segment segment = addSegment(host, optionTitle(name), values, labels);
    for (std::size_t i = 0; i < segment.buttons.size(); ++i) {
        connect(segment.buttons[i], &QToolButton::clicked, this, [this, option = name, value = values[i]] {
            m_session->setOption(option, value);
        });
    }
    m_options[name] = std::move(segment);
}

void InspectorPanel::syncOptions()
{
    for (auto &[name, segment] : m_options) {
        const auto it = m_session->state().options.find(name);
        const std::string current = it == m_session->state().options.end() ? std::string() : it->second;
        for (std::size_t i = 0; i < segment.buttons.size(); ++i) {
            segment.buttons[i]->setChecked(segment.values[i] == current);
        }
    }
}

void InspectorPanel::buildKeyGrid()
{
    m_layout->addWidget(groupLabel(tr("연습할 키"), *m_theme, this));
    auto *grid = new QGridLayout();
    grid->setSpacing(m_theme->spacing(theme::Spacing::Unit));
    for (int i = 0; i < 12; ++i) {
        const theory::PitchClass key = circleOfFourths[static_cast<std::size_t>(i)];
        auto *button = new QToolButton(this);
        button->setText(fromView(theory::flatName(key)));
        button->setCheckable(true);
        button->setAccessibleName(tr("키 %1").arg(button->text()));
        grid->addWidget(button, i / keysPerRow, i % keysPerRow);
        connect(button, &QToolButton::clicked, this, [this, key] {
            m_session->setCurrentKey(key);
        });
        m_keyButtons.push_back(button);
    }
    m_layout->addLayout(grid);
}

void InspectorPanel::syncKeys()
{
    for (int i = 0; i < 12; ++i) {
        const theory::PitchClass key = circleOfFourths[static_cast<std::size_t>(i)];
        m_keyButtons[static_cast<std::size_t>(i)]->setChecked(key == m_session->state().currentKey);
    }
}

void InspectorPanel::syncSelectedChord()
{
    while (m_chordForm->rowCount() > 0) {
        m_chordForm->removeRow(0);
    }
    const int selected = m_session->selectedSlot();
    const std::vector<Slot> &slotList = m_session->slotList();
    if (selected < 0 || std::cmp_greater_equal(selected, slotList.size())) {
        m_chordTitle->clear();
        return;
    }
    const Slot &slot = slotList[static_cast<std::size_t>(selected)];
    m_chordTitle->setText(tr("선택 · %1").arg(slot.name));
    for (const SlotNote &note : slot.notes) {
        const QString where = tr("%1번줄 %2프렛").arg(note.position.string).arg(note.position.fret);
        auto *value =
            new QLabel(note.noteName + QStringLiteral(" (") + note.degreeLabel + QStringLiteral(") · ") + where, this);
        QString role = tr("음");
        if (note.kind == DotKind::Upper) {
            role = tr("어퍼");
        } else if (note.isRoot) {
            role = tr("루트");
        }
        m_chordForm->addRow(role, value);
    }
}

} // namespace cc::ui
