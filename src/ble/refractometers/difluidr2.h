#pragma once

#include "refractometerdevice.h"

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QTimer>

class ScaleBleTransport;

/**
 * DiFluidR2 — BLE driver for the DiFluid R2 Extract refractometer.
 *
 * Concrete RefractometerDevice. Uses ScaleBleTransport for BLE communication
 * (same abstraction as scale drivers, gives us Qt/CoreBluetooth platform
 * switching for free) — it is not a ScaleDevice subclass; a refractometer is
 * not a scale.
 *
 * Protocol: header 0xDF 0xDF, func, cmd, datalen, data, additive checksum.
 * Service 0x00FF, characteristic 0xAA01.
 *
 * Emits tdsChanged on every completed measurement, including device-initiated
 * ones (the physical button on the R2). Physically-impossible readings (the
 * R2's out-of-range error sentinel, above MAX_PLAUSIBLE_TDS) are dropped here
 * so they can never be persisted. This is the only validation the driver does:
 * the sub-threshold lower-bound plausibility filter (sub-3%) and context
 * gating (which shot is loaded) remain the consumer's responsibility.
 */
class DiFluidR2 : public RefractometerDevice {
    Q_OBJECT

public:
    explicit DiFluidR2(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DiFluidR2() override;

    bool isConnected() const override { return m_connected; }
    double tds() const override { return m_tds; }
    double temperature() const override { return m_temperature; }
    bool isMeasuring() const override { return m_measuring; }
    QString name() const override { return m_name; }

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    void disconnectFromDevice() override;
    void requestMeasurement() override;

    // BLE name matching — call before scale detection to prevent misclassification
    static bool isR2Device(const QString& name);

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
    void handlePacket(const QByteArray& packet);
    // The R2 sends its serial number as SERIAL_PART_COUNT packets, each carrying a
    // part index in Data0 followed by SERIAL_PART_BYTES bytes. Parts are spliced at
    // their indicated offset, so arrival order does not matter — BLE notification
    // ordering is not something to assume.
    void handleSerialNumberPart(const QByteArray& data);
    // Shared TDS result path for pack 2 (single test — used by both the app's
    // "Read TDS" button and the physical R2 Start button) and pack 3 (average).
    // Applies the out-of-range sanity gate so every consumer-bound TDS is
    // validated identically regardless of which path produced it.
    void emitTdsResult(quint16 tdsRaw, bool isAverage);
    // Instrumentation: log the refractive index (Data3-6) carried alongside the
    // concentration in pack 2/3. RI is the device's ground-truth optical reading and
    // the cross-check for whether the concentration field is coffee TDS or raw Brix.
    void logRefractiveIndex(const QByteArray& packet, quint8 dataLen);
    bool validateChecksum(const QByteArray& packet) const;
    void sendCommand(const QByteArray& cmd);

#ifdef DECENZA_TESTING
    friend class tst_DiFluidR2;
#endif

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "DiFluid R2";
    QString m_deviceModel;  // From Get-Device-Model query; "DFT-R102" == genuine R2 Extract
    // Serial number, reassembled from its parts. Identity data, not measurement data:
    // it supports telling a genuine R2 Extract from a Brix-reporting variant, where
    // the model string alone has proven insufficient. Empty until every part arrives —
    // a partial serial is never presented as the device's identity.
    static constexpr int SERIAL_PART_COUNT = 3;
    static constexpr int SERIAL_PART_BYTES = 5;
    QString m_serialNumber;
    QByteArray m_serialParts;
    quint8 m_serialPartsSeen = 0;  // bit N set once part N has arrived
    bool m_connected = false;
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
    double m_tds = 0.0;
    double m_temperature = 0.0;
    bool m_measuring = false;
    QTimer m_measurementTimer;
    QTimer m_initTimer;
};
