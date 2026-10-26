// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/appearancepage.h"

#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace cc::ui
{

AppearancePage::AppearancePage(const Theme &theme, Settings &settings, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_theme(&theme)
    , m_settings(&settings)
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("외관"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    addSegment(form,
               setting::Key::AppearanceTheme,
               tr("테마"),
               {{"dark", tr("다크")}, {"light", tr("라이트")}, {"system", tr("시스템 따름")}});
    addSegment(form,
               setting::Key::AppearanceFretboardDotSize,
               tr("지판 점 크기"),
               {{"normal", tr("보통")}, {"large", tr("크게")}});
    addSegment(form,
               setting::Key::AppearanceFontSize,
               tr("글꼴 크기"),
               {{"normal", tr("보통")}, {"large", tr("크게")}});
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("글꼴 크기는 다음 실행부터 전체에 적용됩니다 (커스텀 뷰는 즉시)."), this));
    layout->addStretch();
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

void AppearancePage::addSegment(QFormLayout *form,
                                setting::Key key,
                                const QString &title,
                                const std::map<std::string, QString> &labels)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    Segment segment;
    const auto allowed = nlohmann::json::parse(setting::info(key).valuesJson, nullptr, false);
    for (const auto &entry : allowed) {
        const std::string value = entry.get<std::string>();
        auto *button = new QToolButton(row);
        button->setText(labels.at(value));
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setAccessibleName(title + QStringLiteral(" ") + labels.at(value));
        connect(button, &QToolButton::clicked, this, [this, key, value] {
            m_settings->setValue(key, nlohmann::json(value));
        });
        layout->addWidget(button);
        segment.buttons.push_back(button);
        segment.values.push_back(value);
    }
    layout->addStretch();
    form->addRow(title, row);
    m_segments[key] = std::move(segment);
}

void AppearancePage::syncFromSettings()
{
    for (auto &[key, segment] : m_segments) {
        const std::string current = m_settings->value(key).get<std::string>();
        for (std::size_t i = 0; i < segment.buttons.size(); ++i) {
            segment.buttons[i]->setChecked(segment.values[i] == current);
        }
    }
}

void AppearancePage::restoreDefaults()
{
    for (const auto &[key, segment] : m_segments) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
}

} // namespace cc::ui
