#pragma once

#include "refractometerdevice.h"

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QTimer>
#include <array>
#include <cstdint>

class ScaleBleTransport;

/**
 * DiFluidR1 — BLE driver for the DiFluid R1 refractometer.
 *
 * Protocol (reverse-engineered, see issue #1307 / docs):
 *   - Service 0x1EFF (advertised as 0xE01E).
 *   - 1E01 notify: 16-byte AES-128-ECB ciphertext measurement frame.
 *   - 1E03 read:   12-byte salt → AES session key derivation.
 *   - 1E08 write:  command channel — doDetect = `01 00`. Writes must be
 *                  ATT Write Request (WriteWithResponse), not Write Command;
 *                  the device silently drops Write Commands.
 *
 * Key derivation from the salt:
 *   key[0..5]  = (salt[i] - (i/2)) & 0xFF   for i in 0..5
 *   key[6..15] = 0xAA repeated
 *
 * Plaintext layout (big-endian, 16 bytes):
 *   bytes  0..3 : int32   sample temperature × 0.01 °C
 *   bytes  4..7 : int32   brix × 0.01 %
 *   bytes  8..11: uint32  refractive index × 1e-5
 *   bytes 12..15: int32   raw sample × 0.01
 *
 * The driver exposes brix as the `tds` property so consumers can treat R1 and
 * R2 identically. The out-of-range gate from R2 (`MAX_PLAUSIBLE_TDS`) is
 * shared here in spirit but R1 has no documented sentinel; we still drop
 * physically-impossible readings on the same threshold for safety.
 */
class DiFluidR1 : public RefractometerDevice {
    Q_OBJECT

public:
    explicit DiFluidR1(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DiFluidR1() override;

    bool isConnected() const override { return m_connected; }
    double tds() const override { return m_tds; }
    double temperature() const override { return m_temperature; }
    bool isMeasuring() const override { return m_measuring; }
    QString name() const override { return m_name; }

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    void disconnectFromDevice() override;
    void requestMeasurement() override;

    // BLE name matching — call before scale detection to prevent misclassification.
    // R1 advertises with names starting `DFT_TDJ_`.
    static bool isR1Device(const QString& name);

    // Test-friendly free helpers (pure, no Qt deps beyond QByteArray).
    static std::array<uint8_t, 16> deriveKey(const QByteArray& salt);
    static QByteArray decryptFrame(const QByteArray& ciphertext,
                                   const std::array<uint8_t, 16>& key);
    static bool parsePlaintext(const QByteArray& plaintext,
                               double& outTempC, double& outBrix,
                               double& outRi, double& outRawSample);
    // Build the optional `doWrite(value, label)` echo-back frame (0x06 header +
    // 16-byte AES-ECB ciphertext). Not currently sent by the driver; exposed
    // for the unit-test vector.
    static QByteArray buildDoWriteFrame(double value, const QByteArray& label,
                                        const std::array<uint8_t, 16>& key);

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);
    void onCharacteristicRead(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
    void handleMeasurementFrame(const QByteArray& ciphertext);
    void emitTdsResult(double brix, double tempC);
    void sendCommand(const QByteArray& cmd);

#ifdef DECENZA_TESTING
    friend class tst_DiFluidR1;
#endif

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "DiFluid R1";
    bool m_connected = false;
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
    bool m_keyReady = false;
    double m_tds = 0.0;
    double m_temperature = 0.0;
    bool m_measuring = false;
    std::array<uint8_t, 16> m_key{};
    QTimer m_measurementTimer;
    QTimer m_initTimer;
    QTimer m_saltWatchdog;
};
