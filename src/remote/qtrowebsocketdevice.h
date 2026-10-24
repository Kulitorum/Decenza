// Copyright (C) 2019 Ford Motor Company
// SPDX-License-Identifier: BSD-3-Clause
// Adapted from Qt Remote Objects examples/remoteobjects/websockets.
// Decenza adds bounded buffers, authorization and session ownership.
#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QPointer>

#include <functional>

class QWebSocket;

// Adapts one authenticated QWebSocket connection to the sequential QIODevice
// expected by Qt Remote Objects. Each websocket binary message is one bounded
// transport chunk; text messages are never delivered to QtRO.
class QtRoWebSocketDevice final : public QIODevice
{
    Q_OBJECT

public:
    using AuthorizationCheck = std::function<bool()>;

    static constexpr qsizetype MaximumIncomingMessageBytes = 128 * 1024;
    // Services admit up to 2 MiB of serialized arguments. The byte stream must
    // also accommodate their QtRO envelope; individual WebSocket chunks stay small.
    static constexpr qint64 MaximumPacketBytes = 4 * 1024 * 1024;
    static constexpr qsizetype MaximumBufferedIncomingBytes = MaximumPacketBytes;
    static constexpr qint64 MaximumOutgoingQueuedBytes = MaximumPacketBytes;

    explicit QtRoWebSocketDevice(QWebSocket* socket,
                                 AuthorizationCheck authorizationCheck,
                                 qsizetype maximumIncomingMessageBytes = MaximumIncomingMessageBytes,
                                 qsizetype maximumBufferedIncomingBytes = MaximumBufferedIncomingBytes,
                                 qint64 maximumOutgoingQueuedBytes = MaximumOutgoingQueuedBytes,
                                 QObject* parent = nullptr);
    ~QtRoWebSocketDevice() override;

    qint64 bytesAvailable() const override;
    bool isSequential() const override;
    void close() override;

signals:
    // Qt Remote Objects' QtROExternalIoDevice looks for this exact signal to
    // retire a host-side connection when its external QIODevice disconnects
    // (Qt 6.12 qconnectionfactories.cpp:219-228).
    void disconnected();
    void transportClosed();

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;

private:
    void reject(const QString& reason);

    QPointer<QWebSocket> m_socket;
    AuthorizationCheck m_authorizationCheck;
    qsizetype m_maximumIncomingMessageBytes = MaximumIncomingMessageBytes;
    qsizetype m_maximumBufferedIncomingBytes = MaximumBufferedIncomingBytes;
    qint64 m_maximumOutgoingQueuedBytes = MaximumOutgoingQueuedBytes;
    QByteArray m_readBuffer;
    bool m_transportClosedEmitted = false;
};
