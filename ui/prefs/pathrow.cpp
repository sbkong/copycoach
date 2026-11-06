// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/pathrow.h"

#include "ui/shell/settings.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

namespace cc::ui
{

PathRow::PathRow(Settings &settings, setting::Key key, const QString &placeholder, QWidget *parent)
    : QWidget(parent)
    , m_settings(&settings)
    , m_key(key)
    , m_edit(new QLineEdit(this))
    , m_browse(new QPushButton(tr("찾아보기…"), this))
    , m_clear(new QPushButton(tr("기본값"), this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_edit->setPlaceholderText(placeholder);
    m_edit->setClearButtonEnabled(false);
    layout->addWidget(m_edit, 1);
    layout->addWidget(m_browse);
    layout->addWidget(m_clear);
    connect(m_edit, &QLineEdit::editingFinished, this, [this] {
        if (!m_syncing) {
            m_settings->setValue(m_key, m_edit->text().trimmed());
        }
    });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(this, tr("폴더 선택"), m_edit->text());
        if (!chosen.isEmpty()) {
            m_settings->setValue(m_key, chosen);
        }
    });
    connect(m_clear, &QPushButton::clicked, this, [this] {
        m_settings->setValue(m_key, Settings::defaultValue(m_key));
    });
    syncFromSettings();
}

void PathRow::syncFromSettings()
{
    m_syncing = true;
    m_edit->setText(m_settings->stringValue(m_key));
    m_clear->setEnabled(!m_edit->text().isEmpty());
    m_syncing = false;
}

} // namespace cc::ui
