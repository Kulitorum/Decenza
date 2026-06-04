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
    // Shared TDS result path for pack 2 (single test — used by both the app's
    // "Read TDS" button and the physical R2 Start button) and pack 3 (average).
    // Applies the out-of-range sanity gate so every consumer-bound TDS is
    // validated identically regardless of which path produced it.
    void emitTdsResult(quint16 tdsRaw, bool isAverage);
    bool validateChecksum(const QByteArray& packet) const;
    void sendCommand(const QByteArray& cmd);

#ifdef DECENZA_TESTING
    friend class tst_DiFluidR2;
#endif

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "DiFluid R2";
    bool m_connected = false;
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
    double m_tds = 0.0;
    double m_temperature = 0.0;
    bool m_measuring = false;
    QTimer m_measurementTimer;
    QTimer m_initTimer;
};
