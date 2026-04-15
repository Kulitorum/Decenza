#pragma once

#include "ble/de1transport.h"
#include <QBluetoothUuid>
#include <QByteArray>
#include <QList>
#include <QPair>

// Mock transport that captures BLE writes instead of sending them.
// Used by tst_shotsettings to verify exact wire format.

class MockTransport : public DE1Transport {
    Q_OBJECT
public:
    explicit MockTransport(QObject* parent = nullptr) : DE1Transport(parent) {}

    // Captured writes
    QList<QPair<QBluetoothUuid, QByteArray>> writes;

    // DE1Transport interface
    void write(const QBluetoothUuid& uuid, const QByteArray& data) override {
        writes.append({uuid, data});
    }
    void read(const QBluetoothUuid&) override {}
    void subscribe(const QBluetoothUuid&) override {}
    void subscribeAll() override {}
    void disconnect() override {}
    bool isConnected() const override { return true; }
    QString transportName() const override { return QStringLiteral("Mock"); }

    // Test helpers
    QByteArray lastWriteData() const { return writes.isEmpty() ? QByteArray() : writes.last().second; }
    void clearWrites() { writes.clear(); }

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
