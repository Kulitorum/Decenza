// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
#pragma once

#include <QObject>
#include <QDir>
#include <QTcpServer>

class QTcpSocket;

class StaticFileServer final : public QObject
{
    Q_OBJECT
public:
    explicit StaticFileServer(QString root, QObject* parent = nullptr);
    bool listen(quint16 port);
    QString errorString() const { return m_error; }

private:
    void handleSocket(QTcpSocket* socket);
    static QByteArray mimeType(const QString& path);

    QDir m_root;
    QString m_canonicalRoot;
    QTcpServer m_server;
    QString m_error;
};
