// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
// Adapted from SynQt tests/transport-spike/shared/websocketiodevice.h.
#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QWebSocket;
QT_END_NAMESPACE

class WebSocketIoDevice final : public QIODevice
{
    Q_OBJECT
public:
    explicit WebSocketIoDevice(QWebSocket* socket, QObject* parent = nullptr);
    qint64 bytesAvailable() const override;
    bool isSequential() const override;
    void close() override;

signals:
    void disconnected();

protected:
    qint64 readData(char* data, qint64 maxlen) override;
    qint64 writeData(const char* data, qint64 len) override;

private:
    QPointer<QWebSocket> m_socket;
    QByteArray m_buffer;
};
