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
 * Protocol (reverse-engineered from official DiFluid app v1.2.6):
 *   - Service 0x1EFF (advertised as 0xE01E).
 *   - 1E01 notify: 16-byte AES-128-ECB ciphertext measurement frame.
 *   - 1E03 read:   12-byte salt → AES session key derivation.
 *   - 1E08 write:  command channel — doDetect = `01 00`. Writes must be
 *                  ATT Write Request (WriteWithResponse), not Write Command;
 *                  the device silently drops Write Commands.
 *
 * Key derivation from the salt:
 *   key[0..5]  = (salt[i] - (i >> 1)) & 0xFF   for i in 0..5
 *   key[6..15] = 0xAA repeated
 *
 * Plaintext layout (big-endian, 16 bytes):
 *   bytes  0..3 : int32   sample temperature × 0.01 °C
 *   bytes  4..7 : int32   brix × 0.01 %
 *   bytes  8..11: uint32  refractive index × 1e-5
 *   bytes 12..15: int32   raw sample × 0.01
 *
 * The driver exposes brix as the `tds` property so consumers can treat R1 and
 * R2 identically. The shared MAX_PLAUSIBLE_TDS gate from RefractometerDevice
 * drops physically-impossible readings (defends against bad decrypts here).
 */
class DiFluidR1 : public RefractometerDevice {
    Q_OBJECT

public:
    explicit DiFluidR1(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~DiFluidR1() override;

    bool isConnected() const override { return m_phase == Phase::Ready; }
    double tds() const override { return m_tds; }
    double temperature() const override { return m_temperature; }
    bool isMeasuring() const override { return m_measuring; }
    QString name() const override { return m_name; }

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    void disconnectFromDevice() override;
    void requestMeasurement() override;

    // BLE name matching — call before scale detection to prevent
    // misclassification. R1 advertises with names starting `DFT_TDJ_*`.
    static bool isR1Device(const QString& name);

    // Pure helpers — exposed for unit testing against the captured vectors.
    static std::array<uint8_t, 16> deriveKey(const QByteArray& salt);
    static QByteArray decryptFrame(const QByteArray& ciphertext,
                                   const std::array<uint8_t, 16>& key);
    static bool parsePlaintext(const QByteArray& plaintext,
                               double& outTempC, double& outBrix,
                               double& outRi, double& outRawSample);
    // Builds the doWrite frame: 0x06 header + AES-ECB(int32 value × 100 ‖
    // label length ‖ ASCII label ‖ zero pad).
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
    // Link lifecycle. A single monotonic state replaces the four-flag bag
    // (connected/serviceFound/charsReady/keyReady) the implication chain
    // would have to encode otherwise. m_measuring is orthogonal.
    enum class Phase {
        Disconnected,
        ServiceDiscovery,
        CharacteristicsReady,
        Ready,
    };

    void resetLinkState();
    void handleMeasurementFrame(const QByteArray& ciphertext);
    void emitTdsResult(double brix, double tempC);
    void sendCommand(const QByteArray& cmd);

#ifdef DECENZA_TESTING
    friend class tst_DiFluidR1;
#endif

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "DiFluid R1";
    Phase m_phase = Phase::Disconnected;
    double m_tds = 0.0;
    double m_temperature = 0.0;
    bool m_measuring = false;
    std::array<uint8_t, 16> m_key{};
    QTimer m_measurementTimer;
    QTimer m_initTimer;
    QTimer m_saltWatchdog;
};
