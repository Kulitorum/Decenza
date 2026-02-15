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
    QString type() const override { return m_isPyxis ? "acaiapyxis" : "acaia"; }

public slots:
    void tare() override;
    void sendKeepAlive() override;
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
    bool m_isConnecting = false;  // Track if connection is in progress

    // Timers
    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_initTimer = nullptr;  // Recurring timer for ident/config sequence
    int m_identRetryCount = 0;

    // Message parsing state
    QByteArray m_buffer;

    // Constants
    static constexpr int ACAIA_METADATA_LEN = 5;
    static constexpr int MAX_IDENT_RETRIES = 10;  // Same as de1app
    static constexpr int INIT_TIMER_INTERVAL_MS = 500;  // Ident + config every 500ms
};
