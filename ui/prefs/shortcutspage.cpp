// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/shortcutspage.h"

#include "generated/actionids.h"
#include "ui/prefs/inlinemessage.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/keymap.h"
#include "ui/theme/theme.h"

#include <QAction>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <array>

namespace cc::ui
{

namespace
{

constexpr std::array<const char *, 5> contextTitles{"전역", "루틴", "카피", "편집기", "영상"};

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

QString keysText(const QList<QKeySequence> &keys)
{
    QStringList parts;
    for (const QKeySequence &key : keys) {
        parts.append(key.toString(QKeySequence::NativeText));
    }
    return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" / "));
}

const action::Info *infoOf(const QString &actionId)
{
    for (const action::Info &info : action::infos) {
        const QString id = fromView(info.id);
        if (info.rangeMax == 0 ? id == actionId : actionId.startsWith(id.left(id.indexOf(QLatin1Char('{'))))) {
            return &info;
        }
    }
    return nullptr;
}

} // namespace

// ---- model ---------------------------------------------------------------------------------------------------------

KeymapModel::KeymapModel(const ActionRegistry &actions, const Keymap &keymap, const Theme &theme, QObject *parent)
    : QAbstractTableModel(parent)
    , m_actions(&actions)
    , m_keymap(&keymap)
    , m_theme(&theme)
{
    rebuild();
}

void KeymapModel::setFilter(workspace::Context context, const QString &query)
{
    m_context = context;
    m_query = query.trimmed();
    rebuild();
}

void KeymapModel::setConflictRow(const QString &actionId)
{
    m_conflictId = actionId;
    if (!m_rows.empty()) {
        emit dataChanged(index(0, keyColumn), index(rowCount() - 1, keyColumn));
    }
}

void KeymapModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    for (const QAction *action : m_actions->all()) {
        const QString id = action->objectName();
        if (contextOfActionId(id) != m_context) {
            continue;
        }
        if (!m_query.isEmpty() && !action->text().contains(m_query, Qt::CaseInsensitive)
            && !id.contains(m_query, Qt::CaseInsensitive)) {
            continue;
        }
        m_rows.push_back(id);
    }
    endResetModel();
}

QString KeymapModel::actionIdAt(int row) const
{
    return row >= 0 && std::cmp_less(row, m_rows.size()) ? m_rows[static_cast<std::size_t>(row)] : QString();
}

int KeymapModel::rowOf(const QString &actionId) const
{
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i] == actionId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int KeymapModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int KeymapModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

QVariant KeymapModel::sourceText(const QString &actionId) const
{
    const action::Info *info = infoOf(actionId);
    const QString source = info == nullptr ? QString() : fromView(info->source);
    if (m_keymap->isDefault(actionId)) {
        return source.isEmpty() ? tr("자체") : source;
    }
    return tr("사용자 (기본 %1)").arg(keysText(Keymap::defaultKeys(actionId)));
}

QVariant KeymapModel::data(const QModelIndex &index, int role) const
{
    QString id = actionIdAt(index.row());
    if (id.isEmpty()) {
        return {};
    }
    if (role == idRole) {
        return id;
    }
    if (role == conflictRole) {
        return index.column() == keyColumn && id == m_conflictId;
    }
    if (role == Qt::FontRole && !m_keymap->isDefault(id)) {
        return m_theme->font(theme::TypeStyle::BodyStrong); // changed rows are bold (M8b)
    }
    if (role == Qt::ForegroundRole && index.column() == keyColumn && id == m_conflictId) {
        return m_theme->color(theme::Token::NegativeColorDefault);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (index.column()) {
    case actionColumn:
        return m_actions->all()[static_cast<std::size_t>(0)] != nullptr
            ? QString(infoOf(id) != nullptr ? fromView(infoOf(id)->label) : id)
                  .replace(QStringLiteral("{n}"), id.section(QLatin1Char('-'), -1))
            : id;
    case keyColumn:
        return keysText(m_keymap->keys(id));
    case sourceColumn:
        return sourceText(id);
    default:
        return {};
    }
}

QVariant KeymapModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case actionColumn:
        return tr("동작");
    case keyColumn:
        return tr("키");
    case sourceColumn:
        return tr("출처");
    default:
        return {};
    }
}

// ---- page -----------------------------------------------------------------------------------------------------------

ShortcutsPage::ShortcutsPage(const Theme &theme, ActionRegistry &actions, Keymap &keymap, QWidget *parent)
    : PreferencesPageBase(parent)
    , m_theme(&theme)
    , m_actions(&actions)
    , m_keymap(&keymap)
    , m_model(new KeymapModel(actions, keymap, theme, this))
    , m_preset(new QLabel(this))
    , m_search(new QLineEdit(this))
    , m_tabs(new QTabBar(this))
    , m_table(new QTreeView(this))
    , m_recorder(new QKeySequenceEdit(this))
    , m_assign(new QPushButton(tr("적용"), this))
    , m_itemDefault(new QPushButton(tr("항목 기본값"), this))
    , m_message(new InlineMessage(theme, this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("단축키"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    buildHeader(layout);
    m_table->setModel(m_model);
    m_table->setRootIsDecorated(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->header()->setStretchLastSection(true);
    m_table->setAccessibleName(tr("단축키 표"));
    layout->addWidget(m_table, 1);
    buildRecorder(layout);
    layout->addWidget(m_message);
    connect(m_keymap, &Keymap::keysChanged, this, [this](const QString &) {
        refilter();
        updatePresetLabel();
    });
    refilter();
    updatePresetLabel();
}

void ShortcutsPage::buildHeader(QVBoxLayout *layout)
{
    auto *row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("프리셋"), this));
    m_preset->setFont(m_theme->font(theme::TypeStyle::BodyStrong));
    row->addWidget(m_preset);
    row->addStretch();
    m_search->setPlaceholderText(tr("동작 검색…"));
    m_search->setAccessibleName(tr("동작 검색"));
    m_search->setClearButtonEnabled(true);
    row->addWidget(m_search);
    layout->addLayout(row);
    for (const char *title : contextTitles) {
        m_tabs->addTab(tr(title));
    }
    m_tabs->setAccessibleName(tr("단축키 문맥"));
    layout->addWidget(m_tabs);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) {
        refilter();
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int) {
        refilter();
    });
}

void ShortcutsPage::buildRecorder(QVBoxLayout *layout)
{
    auto *row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("키 녹음"), this));
    m_recorder->setAccessibleName(tr("새 키 조합"));
    m_recorder->setMaximumSequenceLength(1);
    row->addWidget(m_recorder);
    row->addWidget(m_assign);
    row->addWidget(m_itemDefault);
    auto *note = new QLabel(tr("수정키 + 영숫자만, 펑션키 단독 금지"), this);
    note->setFont(m_theme->font(theme::TypeStyle::Caption));
    row->addWidget(note);
    row->addStretch();
    layout->addLayout(row);
    connect(m_assign, &QPushButton::clicked, this, [this] {
        assignKey(m_recorder->keySequence());
    });
    connect(m_recorder, &QKeySequenceEdit::editingFinished, this, [this] {
        assignKey(m_recorder->keySequence());
    });
    connect(m_itemDefault, &QPushButton::clicked, this, &ShortcutsPage::resetSelected);
}

void ShortcutsPage::refilter()
{
    const QString selected = selectedActionId();
    m_model->setFilter(static_cast<workspace::Context>(m_tabs->currentIndex()), m_search->text());
    if (!selected.isEmpty()) {
        selectAction(selected);
    }
}

void ShortcutsPage::updatePresetLabel()
{
    const int changed = m_keymap->overrideCount();
    m_preset->setText(changed == 0 ? tr("Guitar Pro 8 (기본)") : tr("Guitar Pro 8 (기본) · 변경 %1").arg(changed));
}

KeymapModel *ShortcutsPage::model() const
{
    return m_model;
}

QString ShortcutsPage::selectedActionId() const
{
    return m_model->actionIdAt(m_table->currentIndex().row());
}

void ShortcutsPage::selectAction(const QString &actionId)
{
    const workspace::Context context = contextOfActionId(actionId);
    if (m_tabs->currentIndex() != static_cast<int>(context)) {
        m_tabs->setCurrentIndex(static_cast<int>(context));
    }
    const int row = m_model->rowOf(actionId);
    if (row >= 0) {
        m_table->setCurrentIndex(m_model->index(row, KeymapModel::actionColumn));
    }
}

void ShortcutsPage::showConflict(const KeyConflict &conflict, const QString &actionId, const QKeySequence &previous)
{
    const QString text = tr("%1은(는) '%2'와 충돌합니다. 다른 키를 고르거나 그 항목을 바꾸세요.")
                             .arg(conflict.key.toString(QKeySequence::NativeText), conflict.label);
    m_model->setConflictRow(actionId);
    m_message->show(MessageKind::Negative,
                    text,
                    {MessageAction{tr("'%1'(으)로 이동").arg(conflict.label),
                                   [this, other = conflict.actionId] {
                                       m_model->setConflictRow(QString());
                                       m_message->clear();
                                       selectAction(other);
                                   }},
                     MessageAction{tr("되돌리기"), [this, previous] {
                                       m_model->setConflictRow(QString());
                                       m_message->clear();
                                       m_recorder->setKeySequence(previous);
                                   }}});
}

bool ShortcutsPage::assignKey(const QKeySequence &key)
{
    const QString id = selectedActionId();
    if (id.isEmpty()) {
        return false;
    }
    const QList<QKeySequence> current = m_keymap->keys(id);
    const auto conflicts = m_keymap->conflicts(id, key);
    if (!conflicts.empty()) {
        showConflict(conflicts.front(), id, current.isEmpty() ? QKeySequence() : current.first());
        return false;
    }
    m_model->setConflictRow(QString());
    m_message->clear();
    m_keymap->setKeys(id, key.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{key});
    m_keymap->apply(*m_actions);
    static_cast<void>(m_keymap->save());
    return true;
}

void ShortcutsPage::resetSelected()
{
    const QString id = selectedActionId();
    if (id.isEmpty()) {
        return;
    }
    m_keymap->setKeys(id, Keymap::defaultKeys(id));
    m_keymap->apply(*m_actions);
    static_cast<void>(m_keymap->save());
    m_message->clear();
}

void ShortcutsPage::restoreDefaults()
{
    m_keymap->resetAll();
    m_keymap->apply(*m_actions);
    static_cast<void>(m_keymap->save());
    m_message->clear();
    refilter();
    updatePresetLabel();
}

} // namespace cc::ui
