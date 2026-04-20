#pragma once

#include "ble/de1transport.h"
#include <QBluetoothUuid>
#include <QByteArray>
#include <QList>
#include <QPair>

// Mock transport that captures BLE writes instead of sending them. Shared
// across the BLE-facing test suite: tst_shotsettings (wire format),
// tst_profileupload (frame-ACK verification), tst_profilemanager (upload
// retry state machine), tst_mcptools_* (MCP tool integration). Tests that
// need to simulate ACKs can either call ackAllWritesInOrder() or emit
// writeComplete() directly.

class MockTransport : public DE1Transport {
    Q_OBJECT
public:
    explicit MockTransport(QObject* parent = nullptr) : DE1Transport(parent) {}

    // Captured writes
    QList<QPair<QBluetoothUuid, QByteArray>> writes;

    // Captured subscribe() calls. Firmware update uses on-demand A009
    // subscription (not always-on) — tests verify subscribe/unsubscribe
    // timing by looking at this list.
    QList<QBluetoothUuid> subscribes;

    // Simulated connection state. Default true so tests that never touch it
    // behave as if the transport is always up (existing behaviour). Tests
    // that exercise disconnect/reconnect flip it via setConnectedSim(), which
    // also emits the matching DE1Transport::connected/disconnected signal.
    bool m_connected = true;

    // Simulated queue depth for clearQueue() accounting. Tests that
    // exercise the "queue drop → MMR cache invalidated" path bump this
    // before calling clearCommandQueue(); the default of 0 models an
    // idle transport where clearQueue is a no-op drop.
    qsizetype pendingQueueSize = 0;

    // DE1Transport interface
    void write(const QBluetoothUuid& uuid, const QByteArray& data) override {
        writes.append({uuid, data});
    }
    void read(const QBluetoothUuid&) override {}
    void subscribe(const QBluetoothUuid& uuid) override { subscribes.append(uuid); }
    void subscribeAll() override {}
    void disconnect() override {
        if (m_connected) {
            m_connected = false;
            emit disconnected();
        }
    }
    qsizetype clearQueue() override {
        const qsizetype dropped = pendingQueueSize;
        pendingQueueSize = 0;
        return dropped;
    }
    bool isConnected() const override { return m_connected; }
    QString transportName() const override { return QStringLiteral("Mock"); }

    // Test helpers
    QByteArray lastWriteData() const { return writes.isEmpty() ? QByteArray() : writes.last().second; }
    void clearWrites() { writes.clear(); }

    // Flip simulated connection state and emit the matching transport signal.
    // Used by tests that verify behaviour across a disconnect/reconnect cycle.
    void setConnectedSim(bool newState) {
        if (m_connected == newState) return;
        m_connected = newState;
        if (newState) emit connected();
        else emit disconnected();
    }

    // Simulate the BLE stack ACKing every captured write in order, mirroring
    // what BleTransport::onCharacteristicWritten does on the real device.
    // Tests that need to simulate dropped, reordered, or partial ACKs should
    // emit writeComplete() directly instead.
    void ackAllWritesInOrder() {
        for (const auto& w : writes) {
            emit writeComplete(w.first, w.second);
        }
    }
};
