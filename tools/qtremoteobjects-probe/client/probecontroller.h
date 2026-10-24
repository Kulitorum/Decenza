// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
// Adapted from SynQt tests/transport-spike/client/spikecontroller.h.
// Modified for deterministic typed-model, queued-watcher, and reconnect checks.
#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QRemoteObjectPendingCall>
#include <QElapsedTimer>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QRemoteObjectNode;
class QTimer;
class QWebSocket;
QT_END_NAMESPACE

class ProbeSourceReplica;
class WebSocketIoDevice;

class ProbeController final : public QObject
{
    Q_OBJECT
public:
    explicit ProbeController(QUrl url, QObject* parent = nullptr);
    ~ProbeController() override;

private:
    enum class Phase { AwaitInitial, CheckInitial, AwaitMutation, CheckMutation,
                       AwaitModelMutation, CheckModelMutation,
                       AwaitDropReply, AwaitDisconnect, AwaitReconnect, CheckReconnect,
                       Passed, Failed };
    void connectToHost();
    void teardown();
    void scheduleReconnect();
    void onReplicaInitialized();
    void onTick();
    void setPhase(Phase phase, const QString& label);
    void log(const QString& message);
    void fail(const QString& message);
    void callMutation();
    void callModelMutation();
    void callDrop();

    const QUrl m_url;
    QRemoteObjectNode* m_node = nullptr;
    QWebSocket* m_socket = nullptr;
    WebSocketIoDevice* m_device = nullptr;
    ProbeSourceReplica* m_replica = nullptr;
    QAbstractItemModel* m_rowsModel = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_checkTimer = nullptr;
    QElapsedTimer m_elapsed;
    Phase m_phase = Phase::AwaitInitial;
    QString m_lastSignal;
    QString m_methodResult;
    QString m_state = QStringLiteral("starting");
    int m_initializations = 0;
    int m_reconnectDelayMs = 500;
    int m_phaseElapsedAtStart = 0;
    bool m_methodFinished = false;
    bool m_dropReplyReceived = false;
    bool m_disconnectAfterDrop = false;
    bool m_expectedReconnect = false;
    bool m_modelMutationFinished = false;
    QString m_modelMutationResult;
};
