// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
// Adapted from SynQt tests/transport-spike/shared/websocketiodevice.cpp.
#include "websocketiodevice.h"

#include <QWebSocket>

#include <algorithm>
#include <cstring>

WebSocketIoDevice::WebSocketIoDevice(QWebSocket* socket, QObject* parent)
    : QIODevice(parent), m_socket(socket)
{
    open(QIODevice::ReadWrite);
    connect(socket, &QWebSocket::disconnected, this, &WebSocketIoDevice::disconnected);
    connect(socket, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& message) {
        m_buffer.append(message);
        emit readyRead();
    });
    connect(socket, &QWebSocket::bytesWritten, this, &WebSocketIoDevice::bytesWritten);
}

qint64 WebSocketIoDevice::bytesAvailable() const
{
    return QIODevice::bytesAvailable() + m_buffer.size();
}

bool WebSocketIoDevice::isSequential() const
{
    return true;
}

void WebSocketIoDevice::close()
{
    if (m_socket)
        m_socket->close();
}

qint64 WebSocketIoDevice::readData(char* data, qint64 maxlen)
{
    const qint64 size = std::min(maxlen, static_cast<qint64>(m_buffer.size()));
    if (size <= 0)
        return size;
    std::memcpy(data, m_buffer.constData(), static_cast<size_t>(size));
    m_buffer.remove(0, size);
    return size;
}

qint64 WebSocketIoDevice::writeData(const char* data, qint64 len)
{
    return m_socket ? m_socket->sendBinaryMessage(QByteArray(data, static_cast<qsizetype>(len))) : -1;
}
