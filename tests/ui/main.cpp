// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 sbkong
// Catch2 entry with a QApplication (offscreen platform set by ctest) so widget tests can construct windows.
#include <catch2/catch_session.hpp>

#include <QApplication>

int main(int argc, char *argv[])
{
    const QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("CopyCoachTests"));
    return Catch::Session().run(argc, argv);
}
