#pragma once

#include <QObject>
#include <QBluetoothDeviceInfo>
#include <QTimer>

class ScaleBleTransport;

/**
 * DiFluidR2 — BLE driver for the DiFluid R2 Extract refractometer.
 *
 * Standalone QObject (not a ScaleDevice subclass — a refractometer is not a scale).
 * Uses ScaleBleTransport for BLE communication (same abstraction as scale drivers,
 * gives us Qt/CoreBluetooth platform switching for free).
 *
 * Protocol: header 0xDF 0xDF, func, cmd, datalen, data, additive checksum.
 * Service 0x00FF, characteristic 0xAA01.
 *
 * Emits tdsChanged on every completed measurement, including device-initiated
 * ones (physical button on the R2, idle polls). Physically-impossible readings
 * (the R2's out-of-range error sentinel) are dropped here so they can never be
 * persisted; consumers must still gate by context (which shot is loaded).
 */
class DiFluidR2 : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(double tds READ tds NOTIFY tdsChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool measuring READ isMeasuring NOTIFY measuringChanged)
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit DiFluidR2(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DiFluidR2() override;

    bool isConnected() const { return m_connected; }
    double tds() const { return m_tds; }
    double temperature() const { return m_temperature; }
    bool isMeasuring() const { return m_measuring; }
    QString name() const { return m_name; }

    void connectToDevice(const QBluetoothDeviceInfo& device);
    Q_INVOKABLE void disconnectFromDevice();

    Q_INVOKABLE void requestMeasurement();

    // BLE name matching — call before scale detection to prevent misclassification
    static bool isR2Device(const QString& name);

signals:
    void connectedChanged();
    void tdsChanged(double tds);
    void temperatureChanged(double temperature);
    void measuringChanged();
    void measurementComplete();
    void errorOccurred(const QString& error);
    void logMessage(const QString& message);

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
