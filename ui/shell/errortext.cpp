// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/errortext.h"

#include <QCoreApplication>

namespace cc::ui
{

namespace
{

QString tr(const char *text)
{
    return QCoreApplication::translate("errors", text);
}

} // namespace

QString errorText(const Error &error)
{
    const QString subject = QString::fromStdString(error.subject);
    switch (error.code) {
    case ErrorCode::DeviceOpenFailed:
        return tr("오디오 장치를 열 수 없습니다: %1").arg(subject);
    case ErrorCode::DeviceBusy:
        return tr("오디오 장치를 다른 프로그램이 쓰고 있습니다: %1").arg(subject);
    case ErrorCode::FileNotFound:
        return tr("파일이 없습니다: %1").arg(subject);
    case ErrorCode::FileCorrupt:
        return tr("파일을 읽을 수 없습니다 (손상): %1").arg(subject);
    case ErrorCode::SchemaTooNew:
        return tr("더 새로운 버전의 CopyCoach로 저장된 파일입니다: %1").arg(subject);
    case ErrorCode::ModelMissing:
        return tr("필요한 파일이 없습니다: %1").arg(subject);
    case ErrorCode::ModelCorrupt:
        return tr("파일을 불러올 수 없습니다: %1").arg(subject);
    case ErrorCode::Unsupported:
        return tr("지원하지 않는 형식입니다: %1").arg(subject);
    case ErrorCode::DiskFull:
        return tr("디스크 공간이 부족합니다: %1").arg(subject);
    case ErrorCode::WriteFailed:
        return tr("저장하지 못했습니다: %1").arg(subject);
    case ErrorCode::Cancelled:
        return tr("취소했습니다: %1").arg(subject);
    case ErrorCode::NoAccelerator:
        return tr("GPU 가속을 쓸 수 없습니다: %1").arg(subject);
    case ErrorCode::Timeout:
        return tr("시간이 너무 오래 걸려 중단했습니다: %1").arg(subject);
    }
    return QString::fromStdString(error.subject);
}

} // namespace cc::ui
