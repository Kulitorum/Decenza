// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
#include "probecontroller.h"

#include <QGuiApplication>
#include <QUrl>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    ProbeController probe(QUrl(QStringLiteral("ws://127.0.0.1:8898")), &app);
    return app.exec();
}
