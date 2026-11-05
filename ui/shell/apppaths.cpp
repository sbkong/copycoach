// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
#include "ui/shell/apppaths.h"

#include <QCoreApplication>
#include <QStandardPaths>

namespace cc::ui
{

AppPaths AppPaths::detect()
{
    const std::filesystem::path exeDir = QCoreApplication::applicationDirPath().toStdWString();
    const QString user = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return AppPaths{exeDir / "data", exeDir / "sounds", std::filesystem::path(user.toStdWString())};
}

std::filesystem::path AppPaths::theme(bool dark) const
{
    return dataDir / "ui" / (dark ? "theme-dark.json" : "theme-light.json");
}

std::filesystem::path AppPaths::verovioDir() const
{
    return dataDir / "verovio";
}

} // namespace cc::ui
