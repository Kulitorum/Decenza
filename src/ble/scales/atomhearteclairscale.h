#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

class AtomheartEclairScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit AtomheartEclairScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~AtomheartEclairScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::AtomheartEclair); }

public slots:
    void tare() override;
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    // de1app PR #349 gives the Eclair a dedicated timer-reset opcode (0x52) distinct from
    // tare, so the base class default (true) applies — reset/start can be split across the
    // preheating phase.
    void sendKeepAlive() override;

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);
    void onWatchdogTimeout();
    void onTickleTimeout();

private:
    void sendCommand(const QByteArray& cmd);
    bool validateXor(const QByteArray& data);
    void enableNotifications();
    void startWatchdog();
    void tickleWatchdog();
    void stopWatchdog();

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Atomheart Eclair";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;

    // Watchdog: defers connected-state until the first weight frame arrives and
    // re-enables notifications if weight stops streaming. Without this the scale
    // can report "connected" while stuck at 0 g, which fools the no-scale shot-abort
    // safety check. Mirrors VariaAkuScale. Note: re-enabling rewrites the CCCD, which
    // the Eclair is sensitive to (see sendKeepAlive), so retries only fire on no-data,
    // never during healthy streaming.
    QTimer* m_watchdogTimer = nullptr;
    QTimer* m_tickleTimer = nullptr;
    int m_watchdogRetries = 0;
    bool m_updatesReceived = false;
    static constexpr int WATCHDOG_TIMEOUT_MS = 1000;   // Retry interval when no data yet
    static constexpr int TICKLE_TIMEOUT_MS = 2000;     // No-update timeout once streaming
    static constexpr int MAX_WATCHDOG_RETRIES = 5;     // Conservative — Eclair CCCD is touchy
};
