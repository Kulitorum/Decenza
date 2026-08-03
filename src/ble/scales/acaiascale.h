#pragma once

#include "../scaledevice.h"
#include "../transport/scalebletransport.h"
#include <QTimer>
#include <QByteArray>

class AcaiaScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit AcaiaScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~AcaiaScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(m_isPyxis ? ScaleType::AcaiaPyxis : ScaleType::Acaia); }

public slots:
    void tare() override;
    void sendKeepAlive() override;
    // Overridden to false, not merely left at the base default, because these
    // three ARE overridden below — as empty bodies. Without this the scale would
    // look like a timer implementer to anyone reading the override list.
    bool supportsTimer() const override { return false; }
    void startTimer() override {}  // Acaia scales don't support remote timer control
    void stopTimer() override {}
    void resetTimer() override {}

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);
    void sendHeartbeat();
    void sendIdent();
    void sendConfig();
    void enableNotifications();
    void onInitTimer();  // Handles ident/config retry sequence

private:
    void parseResponse(const QByteArray& data);
    void decodeWeight(const QByteArray& payload, int payloadOffset);
    QByteArray encodePacket(uint8_t msgType, const QByteArray& payload);
    void sendCommand(const QByteArray& command);
    void sendTareCommand();  // Internal: sends a single tare command
    void startInitSequence();
    void stopAllTimers();

    ScaleBleTransport* m_transport = nullptr;
    QString m_name = "Acaia";
    bool m_isPyxis = false;  // Auto-detected during service discovery
    bool m_pyxisServiceFound = false;
    bool m_ipsServiceFound = false;
    bool m_characteristicsReady = false;
    bool m_receivingNotifications = false;
    bool m_weightReceived = false;  // Track if we've received weight data
    // One-shot latches so a persistently malformed stream can't flood the log
    // ring (AsyncLogger drops on overflow, so a flood destroys the evidence).
    // All three are reset per connection attempt in connectToDevice().
    bool m_resyncLogged = false;
    bool m_badBatteryLogged = false;
    int m_infoFrameCount = 0;  // msgType 7 frames — reported on init failure

    // Timers
    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_initTimer = nullptr;  // Recurring timer for ident/config sequence
    int m_identRetryCount = 0;

    // Message parsing state
    QByteArray m_buffer;

    // Constants
    static constexpr int ACAIA_METADATA_LEN = 5;
    static constexpr int MAX_ACAIA_PAYLOAD_LEN = 64;  // Sanity ceiling, same as de1app
    static constexpr int MAX_IDENT_RETRIES = 10;  // Same as de1app
    static constexpr int INIT_TIMER_INTERVAL_MS = 500;  // Ident + config every 500ms
};
