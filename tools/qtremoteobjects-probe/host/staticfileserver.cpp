// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
#include "staticfileserver.h"

#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>

#include <utility>

StaticFileServer::StaticFileServer(QString root, QObject* parent)
    : QObject(parent), m_root(std::move(root))
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* socket = m_server.nextPendingConnection())
            handleSocket(socket);
    });
}

bool StaticFileServer::listen(quint16 port)
{
    if (!m_root.exists()) {
        m_error = QStringLiteral("web root does not exist: %1").arg(m_root.absolutePath());
        return false;
    }
    m_canonicalRoot = QFileInfo(m_root.absolutePath()).canonicalFilePath();
    if (m_canonicalRoot.isEmpty()) {
        m_error = QStringLiteral("web root has no canonical filesystem path");
        return false;
    }
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        m_error = m_server.errorString();
        return false;
    }
    return true;
}

void StaticFileServer::handleSocket(QTcpSocket* socket)
{
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        if (socket->property("served").toBool())
            return;
        QByteArray request = socket->property("requestBuffer").toByteArray();
        request.append(socket->readAll());
        if (request.size() > 16 * 1024) {
            socket->setProperty("served", true);
            socket->write("HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        if (!request.contains("\r\n\r\n")) {
            socket->setProperty("requestBuffer", request);
            return;
        }
        const qsizetype lineEnd = request.indexOf("\r\n");
        const QByteArray line = lineEnd >= 0 ? request.left(lineEnd) : request;
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() != 3 || parts.at(0) != "GET") {
            socket->setProperty("served", true);
            socket->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        QByteArray pathBytes = parts.at(1).split('?').value(0);
        if (pathBytes == "/")
            pathBytes = "/qtro_probe_client.html";
        QString relative = QString::fromUtf8(pathBytes);
        while (relative.startsWith('/'))
            relative.remove(0, 1);
        const QString clean = QDir::cleanPath(relative);
        if (clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) {
            socket->setProperty("served", true);
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QString absolute = m_root.filePath(clean);
        const QString canonical = QFileInfo(absolute).canonicalFilePath();
        const QString rootPrefix = m_canonicalRoot + QDir::separator();
        if (canonical.isEmpty() || !canonical.startsWith(rootPrefix)) {
            socket->setProperty("served", true);
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        QFile file(canonical);
        if (!file.open(QIODevice::ReadOnly)) {
            socket->setProperty("served", true);
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QByteArray body = file.readAll();
        socket->setProperty("served", true);
        QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: " + mimeType(canonical)
            + "\r\nContent-Length: " + QByteArray::number(body.size())
            + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        response.append(body);
        socket->write(response);
        socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

QByteArray StaticFileServer::mimeType(const QString& path)
{
    if (path.endsWith(QStringLiteral(".html"))) return "text/html; charset=utf-8";
    if (path.endsWith(QStringLiteral(".js"))) return "application/javascript";
    if (path.endsWith(QStringLiteral(".wasm"))) return "application/wasm";
    if (path.endsWith(QStringLiteral(".data"))) return "application/octet-stream";
    if (path.endsWith(QStringLiteral(".svg"))) return "image/svg+xml";
    if (path.endsWith(QStringLiteral(".png"))) return "image/png";
    return "application/octet-stream";
}
