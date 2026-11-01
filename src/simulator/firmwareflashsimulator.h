#pragma once

#ifdef DECENZA_SIMULATOR

#include "ble/de1transport.h"
#include "ble/blegattqueue.h"
#include "ble/de1logging.h"
#include "core/firmwareheader.h"
#include "ble/firmwarewritetiming.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/firmwarepackets.h"
#include <QTimer>

// An isolated firmware protocol peer, never a radio transport. Reuses GATT
// serialization and timing accounting so UI work is measured on the ACK path.
// Verification checks contiguous packet delivery, not the DE1's cryptography.
class FirmwareFlashSimulator final : public DE1Transport {
public:
    explicit FirmwareFlashSimulator(QObject* parent = nullptr) : DE1Transport(parent) {
        bool ok = false;
        const int delay = qEnvironmentVariableIntValue("DECENZA_SIM_FIRMWARE_ACK_MS", &ok);
        if (ok && delay >= 0 && delay <= 1000) m_ackMs = delay;
    }
    void setAckDelayMs(int ms) { m_ackMs = qBound(0, ms, 1000); }
    qsizetype receivedBytes() const { return m_image.size(); }
    void write(const QBluetoothUuid& uuid, const QByteArray& data) override {
        BleGattQueue::Operation op;
        op.requester = this;
        op.key = uuid;
        op.label = QStringLiteral("simulated firmware write");
        op.issue = [this, uuid, data] {
            const bool chunk = uuid == DE1::Characteristic::WRITE_TO_MMR
                && data.size() == 20 && quint8(data[0]) == 16;
            if (uuid == DE1::Characteristic::FW_MAP_REQUEST) flushTiming();
            if (chunk) m_timing.dispatched(data, FirmwareWriteTiming::nowNs(), m_queue.pendingCount());
            const quint64 generation = m_generation;
            QTimer::singleShot(m_ackMs, Qt::PreciseTimer, this, [this, uuid, data, chunk, generation] {
                if (generation != m_generation) return;
                apply(uuid, data);
                const qint64 entered = FirmwareWriteTiming::nowNs();
                const bool measured = chunk && m_timing.acknowledged(data, entered, entered);
                m_queue.noteSucceeded(this);
                const qint64 released = FirmwareWriteTiming::nowNs();
                emit writeComplete(uuid, data);
                const qint64 consumed = FirmwareWriteTiming::nowNs();
                if (m_queue.pendingCount() == 0) emit queueDrained();
                if (measured) {
                    m_timing.callbackFinished(entered, released, consumed, FirmwareWriteTiming::nowNs());
                    if (m_timing.reportDue()) reportTiming();
                }
            });
            if (chunk) m_timing.issued(FirmwareWriteTiming::nowNs());
        };
        m_queue.submit(std::move(op));
    }
    void read(const QBluetoothUuid&) override {}
    void subscribe(const QBluetoothUuid&) override {}
    void subscribeAll() override {}
    void disconnect() override { clearQueue(); emit disconnected(); }
    bool isConnected() const override { return true; }
    QString transportName() const override { return QStringLiteral("Firmware simulator"); }
    qsizetype clearQueue() override {
        ++m_generation;
        flushTiming();
        return m_queue.forget(this);
    }
private:
    void reportTiming() {
        QString summary = m_timing.report(QStringLiteral("sample"));
        if (!summary.isEmpty()) {
            summary.replace(QStringLiteral("firmware BLE timing"), QStringLiteral("firmware simulator timing"));
            DE1_LOG_TAGGED("FirmwareSimulator", summary);
        }
    }
    void flushTiming() { reportTiming(); m_timing.reset(); }
    void notifyMap(const QByteArray& data) {
        const quint64 generation = m_generation;
        QTimer::singleShot(100, this, [this, data, generation] {
            if (generation == m_generation) emit dataReceived(DE1::Characteristic::FW_MAP_REQUEST, data);
        });
    }
    void apply(const QBluetoothUuid& uuid, const QByteArray& data) {
        if (uuid == DE1::Characteristic::FW_MAP_REQUEST) {
            const auto map = DE1::Firmware::parseFWMapNotification(data);
            if (!map) return;
            if (map->fwToErase == 1) {
                m_image.clear();
                m_corrupt = false;
                notifyMap(DE1::Firmware::buildFWMapRequest(0, 1));
            } else {
                const auto header = DE1::Firmware::parseHeader(m_image);
                const bool complete = header && header->boardMarker == DE1::Firmware::BOARD_MARKER
                    && header->byteCount > 0
                    && quint64(m_image.size()) >= quint64(header->byteCount) + DE1::Firmware::HEADER_SIZE;
                notifyMap(DE1::Firmware::buildFWMapRequest(0, 1,
                    m_corrupt || !complete ? std::array<uint8_t,3>{0,0,0}
                                           : DE1::Firmware::VERIFY_SUCCESS));
            }
        } else if (uuid == DE1::Characteristic::WRITE_TO_MMR) {
            if (data.size() != 20 || quint8(data[0]) != 16) { m_corrupt = true; return; }
            const quint32 address = (quint32(quint8(data[1])) << 16)
                | (quint32(quint8(data[2])) << 8) | quint8(data[3]);
            if (address != quint32(m_image.size())) { m_corrupt = true; return; }
            m_image.append(data.mid(4));
        }
    }
    BleGattQueue m_queue;
    FirmwareWriteTiming m_timing;
    QByteArray m_image;
    quint64 m_generation = 0;
    // Fast by default for Mac smoke tests; the environment override can model
    // slower links without adding work to the ACK consumer itself.
    int m_ackMs = 1;
    bool m_corrupt = false;
};

#endif
