// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: Apache-2.0
// Modified for the Decenza standalone compatibility probe (2026-10-21).
// Adapted from SynQt tests/transport-spike/client/spikecontroller.cpp.
// Modified for deterministic typed-model, queued-watcher, and reconnect checks.
#include "probecontroller.h"

#include "rep_probe_replica.h"
#include "websocketiodevice.h"

#include <QAbstractItemModelReplica>
#include <QDebug>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCall>
#include <QStringList>
#include <QTimer>
#include <QWebSocket>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace {
void installBrowserPanel()
{
#if defined(__EMSCRIPTEN__)
    EM_ASM({
        if (document.getElementById('qtro-probe-panel')) return;
        const style = document.createElement('style');
        style.textContent = '#qtro-probe-panel{position:fixed;z-index:99999;left:12px;right:12px;top:12px;max-height:42vh;overflow:auto;background:#101820;color:#e8f0f5;border:2px solid #54717f;border-radius:8px;padding:12px;font:14px/1.45 monospace;box-shadow:0 3px 16px #0008}#qtro-probe-panel h1{font:600 18px sans-serif;margin:0 0 8px}#qtro-probe-status{font:bold 16px sans-serif;color:#ffcf70;margin-bottom:6px}#qtro-probe-log{white-space:pre-wrap;margin:0}';
        document.head.appendChild(style);
        const panel = document.createElement('section');
        panel.id = 'qtro-probe-panel';
        panel.innerHTML = '<h1>Qt Remote Objects / WebAssembly probe</h1><div id="qtro-probe-status">STARTING</div><pre id="qtro-probe-log">Waiting for the Qt client…</pre>';
        document.body.appendChild(panel);
    });
#endif
}

void browserLog(const QString& message)
{
#if defined(__EMSCRIPTEN__)
    const QByteArray utf8 = message.toUtf8();
    EM_ASM({
        const line = UTF8ToString($0);
        const output = document.getElementById('qtro-probe-log');
        if (output) output.textContent += '\n' + line;
        const status = document.getElementById('qtro-probe-status');
        if (status && line.startsWith('PASS all ')) {
            status.textContent = 'PASS'; status.style.color = '#8de6a0';
        } else if (status && line.startsWith('FAIL')) {
            status.textContent = 'FAIL'; status.style.color = '#ff8787';
        }
    }, utf8.constData());
#endif
}
} // namespace

ProbeController::ProbeController(QUrl url, QObject* parent)
    : QObject(parent), m_url(std::move(url))
{
    installBrowserPanel();
    m_elapsed.start();
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ProbeController::connectToHost);
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(50);
    connect(m_checkTimer, &QTimer::timeout, this, &ProbeController::onTick);
    log(QStringLiteral("START ws=%1; automatic assertions will run").arg(m_url.toString()));
    setPhase(Phase::AwaitInitial, QStringLiteral("initial-connection"));
    connectToHost();
    m_checkTimer->start();
}

ProbeController::~ProbeController()
{
    teardown();
}

void ProbeController::connectToHost()
{
    teardown();
    m_state = QStringLiteral("connecting");
    log(QStringLiteral("CONNECT attempt=%1").arg(m_initializations + 1));
    m_node = new QRemoteObjectNode(this);
    m_socket = new QWebSocket;
    m_device = new WebSocketIoDevice(m_socket, this);
    m_node->addClientSideConnection(m_device);
    m_node->setHeartbeatInterval(1000);
    m_replica = m_node->acquire<ProbeSourceReplica>();
    m_replica->setParent(m_node);
    connect(m_replica, &ProbeSourceReplica::initialized,
            this, &ProbeController::onReplicaInitialized);
    connect(m_replica, &ProbeSourceReplica::pinged, this, [this](const QString& payload) {
        m_lastSignal = payload;
        log(QStringLiteral("SIGNAL pinged=%1").arg(payload));
    });
    connect(m_socket, &QWebSocket::connected, this, [this] {
        log(QStringLiteral("SOCKET connected"));
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this] {
        log(QStringLiteral("SOCKET disconnected"));
        if (m_initializations == 0) {
            scheduleReconnect();
        } else if (m_phase == Phase::AwaitDisconnect && m_dropReplyReceived) {
            m_disconnectAfterDrop = true;
            scheduleReconnect();
        } else {
            fail(QStringLiteral("WebSocket disconnected outside the confirmed drop test"));
        }
    });
    connect(m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        log(QStringLiteral("SOCKET error=%1").arg(m_socket->errorString()));
        if (m_initializations == 0)
            scheduleReconnect();
    });
    m_socket->open(m_url);
}

void ProbeController::onReplicaInitialized()
{
    ++m_initializations;
    m_state = QStringLiteral("connected");
    m_reconnectDelayMs = 500;
    m_rowsModel = m_replica->rows();
    if (auto* replicaModel = qobject_cast<QAbstractItemModelReplica*>(m_rowsModel)) {
        connect(replicaModel, &QAbstractItemModelReplica::initialized, this, [this] {
            log(QStringLiteral("MODEL initialized rows=%1")
                    .arg(m_rowsModel ? m_rowsModel->rowCount() : -1));
        });
    }
    if (m_initializations == 1) {
        setPhase(Phase::CheckInitial, QStringLiteral("initial-state"));
        log(QStringLiteral("REPLICA initialized counter=%1 label=%2")
                .arg(m_replica->counter()).arg(m_replica->label()));
    } else {
        if (!m_expectedReconnect || !m_dropReplyReceived || !m_disconnectAfterDrop) {
            fail(QStringLiteral("Replica initialized without a confirmed test disconnect"));
            return;
        }
        m_expectedReconnect = false;
        setPhase(Phase::CheckReconnect, QStringLiteral("reconnected-state"));
        log(QStringLiteral("REPLICA reinitialized counter=%1 label=%2")
                .arg(m_replica->counter()).arg(m_replica->label()));
    }
}

void ProbeController::onTick()
{
    if (m_phase == Phase::Passed || m_phase == Phase::Failed)
        return;
    if (m_elapsed.elapsed() > 30000) {
        fail(QStringLiteral("Overall probe timed out"));
        return;
    }
    if (m_phaseElapsedAtStart > 0 && m_elapsed.elapsed() - m_phaseElapsedAtStart > 7000) {
        fail(QStringLiteral("Timed out during %1").arg(m_state));
        return;
    }

    if (m_phase == Phase::CheckInitial) {
        if (!m_replica || m_replica->counter() != 10 || m_replica->label() != QStringLiteral("seed")
            || !m_rowsModel || m_rowsModel->rowCount() != 2)
            return;
        if (m_rowsModel->data(m_rowsModel->index(0, 0), Qt::DisplayRole).toString() != QStringLiteral("alpha")
            || m_rowsModel->data(m_rowsModel->index(1, 0), Qt::DisplayRole).toString() != QStringLiteral("beta"))
            return;
        log(QStringLiteral("PASS initial typed properties and model rows replicated"));
        callMutation();
        return;
    }
    if (m_phase == Phase::CheckMutation) {
        if (!m_methodFinished || !m_replica || !m_rowsModel)
            return;
        if (m_methodResult != QStringLiteral("mutation-result:11")
            || m_replica->counter() != 11 || m_replica->label() != QStringLiteral("mutated")
            || m_lastSignal != QStringLiteral("mutation-signal") || m_rowsModel->rowCount() != 3
            || m_rowsModel->data(m_rowsModel->index(2, 0), Qt::DisplayRole).toString() != QStringLiteral("gamma"))
            return;
        log(QStringLiteral("PASS typed property push, signal delivery, model insert, and method return watcher"));
        callModelMutation();
        return;
    }
    if (m_phase == Phase::CheckModelMutation) {
        if (!m_modelMutationFinished || !m_rowsModel)
            return;
        if (m_modelMutationResult != QStringLiteral("model-result:changed-and-removed")
            || m_rowsModel->rowCount() != 2
            || m_rowsModel->data(m_rowsModel->index(0, 0), Qt::DisplayRole).toString()
                != QStringLiteral("alpha-updated")
            || m_rowsModel->data(m_rowsModel->index(1, 0), Qt::DisplayRole).toString()
                != QStringLiteral("gamma"))
            return;
        log(QStringLiteral("PASS model data change and row removal replicated incrementally"));
        callDrop();
        return;
    }
    if (m_phase == Phase::AwaitDisconnect && m_dropReplyReceived)
        return;
    if (m_phase == Phase::CheckReconnect) {
        if (!m_expectedReconnect && m_dropReplyReceived && m_disconnectAfterDrop
            && m_replica && m_rowsModel && m_replica->counter() == 100
            && m_replica->label() == QStringLiteral("reconnected") && m_rowsModel->rowCount() == 4) {
            const QStringList expected{QStringLiteral("fresh-0"), QStringLiteral("fresh-1"),
                                       QStringLiteral("fresh-2"), QStringLiteral("fresh-3")};
            for (int row = 0; row < expected.size(); ++row) {
                if (m_rowsModel->data(m_rowsModel->index(row, 0), Qt::DisplayRole).toString()
                    != expected.at(row))
                    return;
            }
            log(QStringLiteral("PASS forced WebSocket drop, clean reconnect, and fresh replica/model initialization"));
            setPhase(Phase::Passed, QStringLiteral("passed"));
            log(QStringLiteral("PASS all QtRO over WebSocket checks completed; stock queued watcher resolved the method reply"));
        }
        return;
    }
}

void ProbeController::callMutation()
{
    setPhase(Phase::AwaitMutation, QStringLiteral("mutation-reply"));
    m_methodFinished = false;
    m_methodResult.clear();
    const QRemoteObjectPendingReply<QString> reply = m_replica->mutate();
    auto* watcher = new QRemoteObjectPendingCallWatcher(reply, this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [this, watcher, reply] {
        if (reply.error() != QRemoteObjectPendingCall::NoError) {
            fail(QStringLiteral("Remote mutate method returned an error"));
        } else {
            m_methodResult = reply.returnValue();
            m_methodFinished = true;
            setPhase(Phase::CheckMutation, QStringLiteral("verify-mutation"));
            log(QStringLiteral("METHOD watcher finished reply=%1").arg(m_methodResult));
        }
        watcher->deleteLater();
    });
}

void ProbeController::callDrop()
{
    setPhase(Phase::AwaitDropReply, QStringLiteral("drop-reply"));
    const QRemoteObjectPendingReply<QString> reply = m_replica->scheduleDrop();
    auto* watcher = new QRemoteObjectPendingCallWatcher(reply, this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [this, watcher, reply] {
        if (reply.error() != QRemoteObjectPendingCall::NoError
            || reply.returnValue() != QStringLiteral("drop-scheduled")) {
            fail(QStringLiteral("Could not confirm remote drop request"));
        } else {
            m_dropReplyReceived = true;
            setPhase(Phase::AwaitDisconnect, QStringLiteral("forced-drop"));
            log(QStringLiteral("METHOD watcher finished drop schedule; awaiting socket close"));
        }
        watcher->deleteLater();
    });
}

void ProbeController::callModelMutation()
{
    setPhase(Phase::AwaitModelMutation, QStringLiteral("model-mutation-reply"));
    m_modelMutationFinished = false;
    m_modelMutationResult.clear();
    const QRemoteObjectPendingReply<QString> reply = m_replica->mutateModel();
    auto* watcher = new QRemoteObjectPendingCallWatcher(reply, this);
    connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
            [this, watcher, reply] {
        if (reply.error() != QRemoteObjectPendingCall::NoError) {
            fail(QStringLiteral("Remote model mutation returned an error"));
        } else {
            m_modelMutationResult = reply.returnValue();
            m_modelMutationFinished = true;
            setPhase(Phase::CheckModelMutation, QStringLiteral("verify-model-mutation"));
            log(QStringLiteral("METHOD watcher finished model mutation reply=%1")
                    .arg(m_modelMutationResult));
        }
        watcher->deleteLater();
    });
}

void ProbeController::scheduleReconnect()
{
    if (m_reconnectTimer->isActive() || m_phase == Phase::Passed || m_phase == Phase::Failed)
        return;
    if (m_initializations >= 1) {
        if (!m_dropReplyReceived || !m_disconnectAfterDrop) {
            fail(QStringLiteral("Cannot reconnect before the requested drop is confirmed"));
            return;
        }
        m_expectedReconnect = true;
    }
    if (m_initializations >= 1 && m_phase != Phase::CheckReconnect)
        setPhase(Phase::AwaitReconnect, QStringLiteral("reconnect"));
    log(QStringLiteral("RECONNECT scheduled delayMs=%1").arg(m_reconnectDelayMs));
    m_reconnectTimer->start(m_reconnectDelayMs);
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, 4000);
}

void ProbeController::setPhase(Phase phase, const QString& label)
{
    m_phase = phase;
    m_state = label;
    m_phaseElapsedAtStart = m_elapsed.elapsed();
}

void ProbeController::log(const QString& message)
{
    qInfo().noquote() << QStringLiteral("PROBE %1").arg(message);
    browserLog(QStringLiteral("%1").arg(message));
}

void ProbeController::fail(const QString& message)
{
    if (m_phase == Phase::Passed || m_phase == Phase::Failed)
        return;
    m_checkTimer->stop();
    m_reconnectTimer->stop();
    setPhase(Phase::Failed, QStringLiteral("failed"));
    log(QStringLiteral("FAIL %1").arg(message));
}

void ProbeController::teardown()
{
    m_replica = nullptr;
    m_rowsModel = nullptr;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_device) {
        m_device->deleteLater();
        m_device = nullptr;
    }
    if (m_node) {
        m_node->deleteLater();
        m_node = nullptr;
    }
}
