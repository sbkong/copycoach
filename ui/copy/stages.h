// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#pragma once

#include "core/project/project.h"

#include <QCoreApplication>
#include <QString>

#include <array>

// The seven copy stages (copy-spec.md section 2), 1-based like the registry's {n} and the stage.enter event:
// the names and one-line hints the inspector row and the stats dock share.
namespace cc::ui
{

inline QString stageName(int stage)
{
    static constexpr std::array<const char *, project::stageMax> names{
        QT_TRANSLATE_NOOP("stages", "재료"),
        QT_TRANSLATE_NOOP("stages", "멜로디"),
        QT_TRANSLATE_NOOP("stages", "코드·폼"),
        QT_TRANSLATE_NOOP("stages", "프레이즈"),
        QT_TRANSLATE_NOOP("stages", "분석"),
        QT_TRANSLATE_NOOP("stages", "어휘"),
        QT_TRANSLATE_NOOP("stages", "채보"),
    };
    if (stage < project::stageMin || stage > project::stageMax) {
        return {};
    }
    return QCoreApplication::translate("stages", names[static_cast<std::size_t>(stage - 1)]);
}

inline QString stageHint(int stage)
{
    static constexpr std::array<const char *, project::stageMax> hints{
        QT_TRANSLATE_NOOP("stages", "음원·영상·악보를 모으고 키·박자·템포를 확인한다"),
        QT_TRANSLATE_NOOP("stages", "멜로디를 듣고 노래하고 적는다"),
        QT_TRANSLATE_NOOP("stages", "코드 기호와 섹션을 채운다"),
        QT_TRANSLATE_NOOP("stages", "구간을 저장하고 감속 루프로 익힌다"),
        QT_TRANSLATE_NOOP("stages", "구간 안 음을 현재 코드 기준 도수로 본다"),
        QT_TRANSLATE_NOOP("stages", "프레이즈를 저장하고 다른 키로 본다"),
        QT_TRANSLATE_NOOP("stages", "MusicXML로 내보낸다"),
    };
    if (stage < project::stageMin || stage > project::stageMax) {
        return {};
    }
    return QCoreApplication::translate("stages", hints[static_cast<std::size_t>(stage - 1)]);
}

} // namespace cc::ui
