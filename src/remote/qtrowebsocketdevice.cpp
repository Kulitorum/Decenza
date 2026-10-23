// Copyright (C) 2019 Ford Motor Company
// SPDX-License-Identifier: BSD-3-Clause
// Adapted from Qt Remote Objects examples/remoteobjects/websockets.
// Decenza adds bounded buffers, authorization and session ownership.
#include "qtrowebsocketdevice.h"

#include <QWebSocket>
#include <QWebSocketProtocol>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {
constexpr qint64 kMaximumWebSocketMessageBytes = 2 * 1024 * 1024;
}

QtRoWebSocketDevice::QtRoWebSocketDevice(QWebSocket* socket,
                                         AuthorizationCheck authorizationCheck,
                                         qsizetype maximumIncomingMessageBytes,
                                         qsizetype maximumBufferedIncomingBytes,
                                         qint64 maximumOutgoingQueuedBytes,
                                         QObject* parent)
    : QIODevice(parent)
    , m_socket(socket)
    , m_authorizationCheck(std::move(authorizationCheck))
    , m_maximumIncomingMessageBytes(maximumIncomingMessageBytes)
    , m_maximumBufferedIncomingBytes(maximumBufferedIncomingBytes)
    , m_maximumOutgoingQueuedBytes(maximumOutgoingQueuedBytes)
{
    if (!socket || maximumIncomingMessageBytes <= 0
        || maximumBufferedIncomingBytes < maximumIncomingMessageBytes
        || maximumOutgoingQueuedBytes <= 0) {
        if (socket)
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated,
                          QStringLiteral("Invalid QtRO transport limits"));
        return;
    }

    socket->setMaxAllowedIncomingFrameSize(static_cast<quint64>(maximumIncomingMessageBytes));
    socket->setMaxAllowedIncomingMessageSize(static_cast<quint64>(maximumIncomingMessageBytes));
    socket->setReadBufferSize(static_cast<qint64>(maximumBufferedIncomingBytes));
    open(QIODevice::ReadWrite);

    connect(socket, &QWebSocket::binaryMessageReceived, this,
            [this](const QByteArray& message) {
        if (!m_socket)
            return;
        if (m_authorizationCheck && !m_authorizationCheck()) {
            reject(QStringLiteral("Session expired"));
            return;
        }
        if (message.size() > m_maximumIncomingMessageBytes
            || m_readBuffer.size() + message.size() > m_maximumBufferedIncomingBytes) {
            reject(QStringLiteral("QtRO input buffer limit exceeded"));
            return;
        }
        m_readBuffer.append(message);
        emit readyRead();
    });
    connect(socket, &QWebSocket::textMessageReceived, this,
            [this](const QString&) {
        reject(QStringLiteral("QtRO transport accepts binary messages only"));
    });
    connect(socket, &QWebSocket::bytesWritten, this, &QIODevice::bytesWritten);
    connect(socket, &QWebSocket::disconnected, this, [this] {
        if (isOpen())
            QIODevice::close();
        if (!m_transportClosedEmitted) {
            m_transportClosedEmitted = true;
            emit disconnected();
            emit transportClosed();
        }
    });
}

QtRoWebSocketDevice::~QtRoWebSocketDevice() = default;

qint64 QtRoWebSocketDevice::bytesAvailable() const
{
    return QIODevice::bytesAvailable() + m_readBuffer.size();
}

bool QtRoWebSocketDevice::isSequential() const
{
    return true;
}

void QtRoWebSocketDevice::close()
{
    QIODevice::close();
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->close();
}

qint64 QtRoWebSocketDevice::readData(char* data, qint64 maxlen)
{
    if (!data || maxlen <= 0 || m_readBuffer.isEmpty())
        return 0;

    const qsizetype amount = std::min<qsizetype>(maxlen, m_readBuffer.size());
    std::memcpy(data, m_readBuffer.constData(), static_cast<size_t>(amount));
    m_readBuffer.remove(0, amount);
    return amount;
}

qint64 QtRoWebSocketDevice::writeData(const char* data, qint64 len)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState
        || !data || len <= 0 || len > kMaximumWebSocketMessageBytes
        || len > m_maximumOutgoingQueuedBytes) {
        return -1;
    }
    if (m_socket->bytesToWrite() + len > m_maximumOutgoingQueuedBytes) {
        reject(QStringLiteral("QtRO output queue limit exceeded"));
        return -1;
    }

    const qint64 written = m_socket->sendBinaryMessage(
        QByteArray(data, static_cast<qsizetype>(len)));
    return written == len ? len : -1;
}

void QtRoWebSocketDevice::reject(const QString& reason)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->close(QWebSocketProtocol::CloseCodePolicyViolated, reason);
}
