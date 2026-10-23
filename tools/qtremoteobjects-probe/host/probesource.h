// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
#pragma once

#include "rep_probe_source.h"

#include <QPointer>
#include <QList>
#include <QStandardItemModel>
#include <QWebSocket>

class ProbeSource final : public ProbeSourceSimpleSource
{
    Q_OBJECT
public:
    explicit ProbeSource(QList<QPointer<QWebSocket>>* sockets, QObject* parent = nullptr);
    QString mutate() override;
    QString mutateModel() override;
    QString scheduleDrop() override;
    void resetAfterReconnect();

private:
    void resetModel(const QStringList& rows);

    QList<QPointer<QWebSocket>>* m_sockets = nullptr;
    QStandardItemModel m_model;
};
