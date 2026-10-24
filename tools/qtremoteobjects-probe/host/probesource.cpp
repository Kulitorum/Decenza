// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
#include "probesource.h"

#include <QStandardItem>
#include <QStringList>
#include <QTimer>

ProbeSource::ProbeSource(QList<QPointer<QWebSocket>>* sockets, QObject* parent)
    : ProbeSourceSimpleSource(parent), m_sockets(sockets)
{
    setCounter(10);
    setLabel(QStringLiteral("seed"));
    resetModel({QStringLiteral("alpha"), QStringLiteral("beta")});
    setRows(&m_model);
}

QString ProbeSource::mutate()
{
    setCounter(counter() + 1);
    setLabel(QStringLiteral("mutated"));
    m_model.appendRow(new QStandardItem(QStringLiteral("gamma")));
    emit pinged(QStringLiteral("mutation-signal"));
    return QStringLiteral("mutation-result:%1").arg(counter());
}

QString ProbeSource::mutateModel()
{
    if (m_model.rowCount() != 3)
        return QStringLiteral("model-precondition-failed");
    m_model.setData(m_model.index(0, 0), QStringLiteral("alpha-updated"), Qt::DisplayRole);
    m_model.removeRow(1);
    return QStringLiteral("model-result:changed-and-removed");
}

QString ProbeSource::scheduleDrop()
{
    QTimer::singleShot(150, this, [this] {
        if (!m_sockets)
            return;
        const auto sockets = *m_sockets;
        for (const QPointer<QWebSocket>& socket : sockets) {
            if (socket)
                socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                              QStringLiteral("probe forced transport drop"));
        }
        QTimer::singleShot(350, this, &ProbeSource::resetAfterReconnect);
    });
    return QStringLiteral("drop-scheduled");
}

void ProbeSource::resetAfterReconnect()
{
    setCounter(100);
    setLabel(QStringLiteral("reconnected"));
    resetModel({QStringLiteral("fresh-0"), QStringLiteral("fresh-1"),
                QStringLiteral("fresh-2"), QStringLiteral("fresh-3")});
}

void ProbeSource::resetModel(const QStringList& rows)
{
    m_model.clear();
    m_model.setItemRoleNames({{Qt::DisplayRole, QByteArrayLiteral("display")}});
    for (const QString& row : rows)
        m_model.appendRow(new QStandardItem(row));
}
