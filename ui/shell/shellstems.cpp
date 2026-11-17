// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/shell.h"

#include "core/predict/ortsession.h"
#include "core/separation/separator.h"
#include "core/separation/spleetermodel.h"
#include "ui/audio/audioservice.h"
#include "ui/copy/modelfiles.h"
#include "ui/copy/projectsession.h"
#include "ui/copy/stemmixer.h"
#include "ui/jobs/jobservice.h"
#include "ui/shell/actionregistry.h"
#include "ui/shell/errortext.h"
#include "ui/shell/settings.h"

#include <QDockWidget>
#include <QElapsedTimer>
#include <QUrl>

#include <array>
#include <chrono>
#include <memory>
#include <optional>

// Stem separation in the copy workspace (copy-spec.md section 4 "스템", states.md section 2): copy.stem.separate
// runs the engine the models on this machine allow (a user-converted htdemucs file first, research use only;
// otherwise Spleeter, fetched on first use with the shared inline offer) as a StemSeparation job that writes
// stems/<source id>/<stem>.flac. A project whose active version has those files plays the stems instead of
// the recording, and the mixer dock's rows (and copy.stem.mute/solo-{n}) drive the audio service.
namespace cc::ui
{

using action::Id;

namespace
{

constexpr int stemActionRange = 6; // copy.stem.mute-{n} / solo-{n} range in the registry
constexpr std::uint64_t mebibyte = 1024ULL * 1024ULL;

/// The first `count` of the six names, in separation::stemNames order.
QStringList stemLabels(std::size_t count)
{
    const QStringList all{Shell::tr("보컬"),
                          Shell::tr("드럼"),
                          Shell::tr("베이스"),
                          Shell::tr("나머지"),
                          Shell::tr("기타"),
                          Shell::tr("건반")};
    return all.mid(0, static_cast<qsizetype>(count));
}

QString engineName(separation::StemEngine engine)
{
    return engine == separation::StemEngine::Htdemucs ? Shell::tr("htdemucs (연구용)") : QStringLiteral("Spleeter");
}

} // namespace

void Shell::wireStemActions()
{
    connect(m_actions->action(Id::CopyStemSeparate), &QAction::triggered, this, &Shell::runStemSeparation);
    connect(m_stemMixer,
            &StemMixerPanel::separateRequested,
            m_actions->action(Id::CopyStemSeparate),
            &QAction::trigger);
    connect(m_stemMixer, &StemMixerPanel::muteChanged, this, [this](std::size_t stem, bool on) {
        m_audio->setStemMute(stem, on);
    });
    connect(m_stemMixer, &StemMixerPanel::soloChanged, this, [this](std::size_t stem, bool on) {
        m_audio->setStemSolo(stem, on);
    });
    connect(m_stemMixer, &StemMixerPanel::gainChanged, this, [this](std::size_t stem, float gain) {
        m_audio->setStemGain(stem, gain);
    });
    for (int n = 1; n <= stemActionRange; ++n) {
        connect(m_actions->action(Id::CopyStemMuteN, n), &QAction::triggered, this, [this, n] {
            m_stemMixer->toggleMute(static_cast<std::size_t>(n - 1));
        });
        connect(m_actions->action(Id::CopyStemSoloN, n), &QAction::triggered, this, [this, n] {
            m_stemMixer->toggleSolo(static_cast<std::size_t>(n - 1));
        });
    }
    updateStemMixer();
    connect(m_settings, &Settings::valueChanged, this, [this](setting::Key key) {
        if (key == setting::Key::SeparationAccelerator) {
            applyAcceleratorSetting();
        }
    });
    applyAcceleratorSetting();
}

/// auto = WebGPU when a device answers, else CPU; "cuda" counts as auto until the CUDA pack exists.
void Shell::applyAcceleratorSetting()
{
    const QString value = m_settings->stringValue(setting::Key::SeparationAccelerator);
    predict::Accelerator accelerator = predict::Accelerator::Auto;
    if (value == QStringLiteral("cpu")) {
        accelerator = predict::Accelerator::Cpu;
    } else if (value == QStringLiteral("webgpu")) {
        accelerator = predict::Accelerator::WebGpu;
    }
    predict::setDefaultAccelerator(accelerator);
}

QString Shell::providerLabel()
{
    const std::string_view name = predict::providerName(predict::lastProvider());
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

/// The user's own htdemucs conversion wins when present (never downloaded by the app); the public path is
/// the Spleeter file from the release.
std::optional<separation::StemModel> Shell::stemModel() const
{
    const std::filesystem::path research = htdemucsDir(*m_paths) / separation::htdemucsFileName;
    if (std::filesystem::exists(research)) {
        return separation::StemModel{separation::StemEngine::Htdemucs, research};
    }
    if (modelPresent(spleeterDir(*m_paths), separation::spleeterModel)) {
        return separation::StemModel{separation::StemEngine::Spleeter,
                                     modelPath(spleeterDir(*m_paths), separation::spleeterModel)};
    }
    return std::nullopt;
}

std::optional<std::vector<std::filesystem::path>> Shell::cachedStems(const model::Id &sourceId) const
{
    return separation::cachedStemFiles(m_project->stemsDir(sourceId));
}

void Shell::runStemSeparation()
{
    if (!m_project->activeSourcePath()) {
        showStatus(tr("스템 분리는 음원이 있어야 합니다 (파일 › 불러오기…)"));
        return;
    }
    if (m_separating) {
        showStatus(tr("스템 분리가 이미 진행 중입니다"));
        return;
    }
    const std::optional<separation::StemModel> model = stemModel();
    if (model) {
        revealStemMixer(); // the progress and, after, the rows live there
        startStemSeparation(*model);
        return;
    }
    offerModelDownload({tr("스템 분리 모델이 아직 없습니다 (Spleeter, %1 MB). 공식 가중치에서 변환한 파일을 "
                           "GitHub 릴리스에서 내려받을까요? 앱에는 동봉되지 않습니다")
                            .arg(separation::spleeterModel.size / mebibyte),
                        QUrl(QString::fromUtf8(separation::spleeterReleaseUrl.data(),
                                               static_cast<qsizetype>(separation::spleeterReleaseUrl.size()))),
                        spleeterDir(*m_paths),
                        {separation::spleeterModel},
                        [this] {
                            runStemSeparation();
                        }});
}

/// The StemSeparation job: decode, the engine, a FLAC file per stem (four, six with htdemucs_6s); the stems
/// replace the recording when done.
void Shell::startStemSeparation(const separation::StemModel &model)
{
    const auto path = m_project->activeSourcePath();
    const auto sourceId = m_project->activeSourceId();
    if (!path || !sourceId) {
        return;
    }
    const std::filesystem::path outDir = m_project->stemsDir(*sourceId);
    const jobs::Work work = [audio = *path, model, outDir](const std::stop_token &stop,
                                                           const jobs::Report &report) -> Result<void> {
        const auto files = separation::separateToFiles(audio, model, outDir, stop, report);
        return files ? Result<void>{} : std::unexpected(files.error());
    };
    const auto started = std::make_shared<QElapsedTimer>();
    started->start();
    if (m_jobs == nullptr) { // tests: inline
        const std::stop_source never;
        const Result<void> r = work(never.get_token(), [](double) { });
        if (r) {
            finishStemSeparation(model.engine, started->elapsed());
        } else {
            showStatus(errorText(r.error()));
        }
        return;
    }
    m_separating = true;
    m_stemMixer->setBusy(true);
    const jobs::JobId id = m_jobs->enqueue(jobs::JobKind::StemSeparation, work);
    connect(m_jobs,
            &JobService::finished,
            this,
            [this, id, started, engine = model.engine](const jobs::JobEvent &event) {
                if (event.id != id) {
                    return;
                }
                m_separating = false;
                m_stemMixer->setBusy(false);
                if (!event.error) {
                    finishStemSeparation(engine, started->elapsed());
                }
            });
}

/// ia.md section 7: "완료 · 모델 · 소요" is status-bar text, not a chip in the mixer.
void Shell::finishStemSeparation(separation::StemEngine engine, qint64 elapsedMs)
{
    pushCopyScore(); // reloads the recording as its stems
    constexpr double msPerSecond = 1000.0;
    showStatus(tr("스템 분리 완료 · %1 · %2 s · %3")
                   .arg(engineName(engine))
                   .arg(static_cast<double>(elapsedMs) / msPerSecond, 0, 'f', 1)
                   .arg(providerLabel()));
}

/// Shows the mixer dock and gives it the height of its rows: a restored layout may hold it squeezed to a
/// sliver under the inspector.
void Shell::revealStemMixer()
{
    setDockVisible(panel::Id::DockStemMixer, true);
    const auto dock = m_docks.find(panel::Id::DockStemMixer);
    if (dock != m_docks.end() && dock->second->isVisible()) {
        resizeDocks({dock->second}, {dock->second->sizeHint().height()}, Qt::Vertical);
    }
}

/// Rows when the audio service plays stems (a loaded set of four or six), the button otherwise.
void Shell::updateStemMixer()
{
    const std::size_t count = m_audio->sourceCount();
    if (separation::isStemSet(count)) {
        if (m_stemMixer->stemCount() != count) {
            m_stemMixer->setStems(stemLabels(count));
            revealStemMixer(); // stems just loaded: show where they are mixed
        }
    } else {
        m_stemMixer->clear();
    }
}

} // namespace cc::ui
