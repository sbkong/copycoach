// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/prefs/separationpage.h"

#include "core/predict/beatthismodel.h"
#include "ui/copy/modelfiles.h"
#include "ui/jobs/jobservice.h"
#include "ui/prefs/pathrow.h"
#include "ui/shell/settings.h"
#include "ui/theme/theme.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace cc::ui
{

namespace
{

constexpr int maxCacheLimitMb = 65536;
constexpr int cacheStepMb = 512;

QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

} // namespace

SeparationPage::SeparationPage(const Theme &theme,
                               Settings &settings,
                               std::optional<std::filesystem::path> modelsDir,
                               JobService *jobs,
                               QWidget *parent)
    : PreferencesPageBase(parent)
    , m_settings(&settings)
    , m_modelsDir(std::move(modelsDir))
    , m_jobs(jobs)
    , m_beatStatus(new QLabel(this))
    , m_download(new QPushButton(tr("내려받기"), this))
    , m_beatModel(new QComboBox(this))
    , m_accelerator(new QComboBox(this))
    , m_modelDir(new PathRow(settings, setting::Key::SeparationModelDir, tr("앱 데이터 폴더"), this))
    , m_cacheDir(new PathRow(settings, setting::Key::SeparationCacheDir, tr("프로젝트의 stems/ 폴더"), this))
    , m_cacheLimit(new QSpinBox(this))
{
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("스템 분리"), this);
    heading->setFont(theme.font(theme::TypeStyle::Subtitle));
    layout->addWidget(heading);
    auto *form = new QFormLayout();
    buildModelRows(form);
    m_accelerator->addItem(tr("자동"), QStringLiteral("auto"));
    m_accelerator->addItem(tr("WebGPU"), QStringLiteral("webgpu"));
    m_accelerator->addItem(tr("CUDA 팩"), QStringLiteral("cuda"));
    m_accelerator->addItem(tr("CPU"), QStringLiteral("cpu"));
    m_accelerator->setAccessibleName(tr("가속 경로"));
    form->addRow(tr("가속 경로"), m_accelerator);
    form->addRow(QString(), new QLabel(tr("지금은 CPU만 씁니다. 가속 경로는 스템 분리와 함께 켜집니다."), this));
    m_modelDir->setAccessibleName(tr("모델 폴더"));
    form->addRow(tr("모델 폴더"), m_modelDir);
    m_cacheDir->setAccessibleName(tr("캐시 폴더"));
    form->addRow(tr("캐시 폴더"), m_cacheDir);
    m_cacheLimit->setRange(0, maxCacheLimitMb);
    m_cacheLimit->setSingleStep(cacheStepMb);
    m_cacheLimit->setSuffix(tr(" MB"));
    m_cacheLimit->setSpecialValueText(tr("무제한"));
    m_cacheLimit->setAccessibleName(tr("캐시 용량"));
    form->addRow(tr("캐시 용량"), m_cacheLimit);
    layout->addLayout(form);
    layout->addStretch();
    bindControls();
    syncFromSettings();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key) {
        syncFromSettings();
    });
}

/// states.md section 2: model status "있음 · 버전 · 경로" with the download next to it.
void SeparationPage::buildModelRows(QFormLayout *form)
{
    form->addRow(tr("스템 분리 모델"), new QLabel(tr("htdemucs — 스템 분리는 아직 구현되지 않았습니다."), this));
    auto *beatRow = new QWidget(this);
    auto *beatLayout = new QHBoxLayout(beatRow);
    beatLayout->setContentsMargins(0, 0, 0, 0);
    m_beatStatus->setWordWrap(true);
    m_beatStatus->setAccessibleName(tr("박 예측 모델 상태"));
    m_download->setAccessibleName(tr("박 예측 모델 내려받기"));
    m_download->setToolTip(tr("공식 가중치에서 변환한 파일을 GitHub 릴리스에서 받습니다. 앱에는 동봉되지 않습니다."));
    beatLayout->addWidget(m_beatStatus, 1);
    beatLayout->addWidget(m_download);
    form->addRow(tr("박 예측 모델"), beatRow);
    m_beatModel->addItem(tr("final (83 MB, 기본)"), QStringLiteral("final"));
    m_beatModel->addItem(tr("small (10 MB, 저사양)"), QStringLiteral("small"));
    m_beatModel->setAccessibleName(tr("박 예측 모델 선택"));
    form->addRow(tr("박 예측에 쓸 모델"), m_beatModel);
    connect(m_download, &QPushButton::clicked, this, &SeparationPage::downloadBeatModels);
}

void SeparationPage::bindCombo(QComboBox *combo, setting::Key key)
{
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, key](int index) {
        if (!m_syncing && index >= 0) {
            m_settings->setValue(key, combo->itemData(index).toString());
        }
    });
}

void SeparationPage::bindControls()
{
    bindCombo(m_beatModel, setting::Key::CopyPredictBeatModel);
    bindCombo(m_accelerator, setting::Key::SeparationAccelerator);
    connect(m_cacheLimit, &QSpinBox::valueChanged, this, [this](int mb) {
        if (!m_syncing) {
            m_settings->setValue(setting::Key::SeparationCacheLimitMb, nlohmann::json(mb));
        }
    });
}

void SeparationPage::syncFromSettings()
{
    m_syncing = true;
    m_beatModel->setCurrentIndex(
        std::max(0, m_beatModel->findData(m_settings->stringValue(setting::Key::CopyPredictBeatModel))));
    m_accelerator->setCurrentIndex(
        std::max(0, m_accelerator->findData(m_settings->stringValue(setting::Key::SeparationAccelerator))));
    m_cacheLimit->setValue(m_settings->intValue(setting::Key::SeparationCacheLimitMb));
    m_modelDir->syncFromSettings();
    m_cacheDir->syncFromSettings();
    m_syncing = false;
    refreshModelStatus();
}

QString SeparationPage::beatModelStatus() const
{
    return m_beatStatus->text();
}

/// Which of the two models (and the mel matrix) are on disk; the button fetches whatever is missing.
void SeparationPage::refreshModelStatus()
{
    if (m_downloading) {
        m_beatStatus->setText(tr("내려받는 중… (상태바에서 진행률·취소)"));
        m_download->setEnabled(false);
        return;
    }
    if (!m_modelsDir) {
        m_beatStatus->setText(tr("없음 (앱 데이터 폴더를 알 수 없음)"));
        m_download->setEnabled(false);
        return;
    }
    QStringList present;
    const bool filterbank = modelPresent(*m_modelsDir, predict::beatThisFilterbank);
    if (filterbank && modelPresent(*m_modelsDir, predict::beatThisFinal)) {
        present << QStringLiteral("final");
    }
    if (filterbank && modelPresent(*m_modelsDir, predict::beatThisSmall)) {
        present << QStringLiteral("small");
    }
    const QString where = QString::fromStdWString(m_modelsDir->wstring());
    m_beatStatus->setText(present.isEmpty() ? tr("없음 · %1").arg(where)
                                            : tr("있음 · %1 · %2").arg(present.join(QStringLiteral(" · ")), where));
    m_download->setEnabled(m_jobs != nullptr && present.size() < 2);
}

void SeparationPage::downloadBeatModels()
{
    if (m_jobs == nullptr || !m_modelsDir) {
        return;
    }
    std::vector<predict::ModelFile> all{predict::beatThisFilterbank, predict::beatThisFinal, predict::beatThisSmall};
    const std::vector<predict::ModelFile> missing = missingModels(*m_modelsDir, all);
    if (missing.empty()) {
        refreshModelStatus();
        return;
    }
    const QUrl base(fromView(predict::beatThisReleaseUrl));
    const jobs::JobId id = m_jobs->enqueue(jobs::JobKind::ModelDownload, downloadModels(base, *m_modelsDir, missing));
    m_downloading = true;
    refreshModelStatus();
    connect(m_jobs, &JobService::finished, this, [this, id](const jobs::JobEvent &event) {
        if (event.id == id) {
            m_downloading = false;
            refreshModelStatus();
        }
    });
}

void SeparationPage::restoreDefaults()
{
    for (const setting::Key key : {setting::Key::CopyPredictBeatModel,
                                   setting::Key::SeparationAccelerator,
                                   setting::Key::SeparationModelDir,
                                   setting::Key::SeparationCacheDir,
                                   setting::Key::SeparationCacheLimitMb}) {
        m_settings->setValue(key, Settings::defaultValue(key));
    }
}

} // namespace cc::ui
