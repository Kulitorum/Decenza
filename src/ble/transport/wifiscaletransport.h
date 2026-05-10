#pragma once

#include "scalebletransport.h"
#include <QByteArray>
#include <QString>

class QTcpSocket;
class QTimer;

// TCP adapter for the Decent Scale 7-byte frame protocol. Implements the
// BLE-shaped transport interface so DecentScale (the protocol consumer) can
// run unchanged: TCP byte streams are parsed into 7-byte frames and surfaced
// as synthetic `characteristicChanged(Scale::Decent::READ, frame)` events,
// and BLE-only methods (discoverServices/enableNotifications) are turned into
// queued no-op signal emissions so the consumer's wake sequence proceeds.
//
// The factory sets the target with setTarget(ip, port) before triggering the
// usual `DecentScale::connectToDevice(deviceInfo)` flow. The deviceInfo
// arguments are ignored by this transport — only the pre-set target matters.
//
// See `decenza-scale-connectivity` capability spec for the full requirement
// set this transport implements.
class WifiScaleTransport : public ScaleBleTransport {
    Q_OBJECT

public:
    explicit WifiScaleTransport(QObject* parent = nullptr);
    ~WifiScaleTransport() override;

    // Set the TCP destination. Must be called before connectToDevice().
    void setTarget(const QString& ip, int port);

    // BLE-shaped overrides. The QString/QBluetoothDeviceInfo arguments are
    // ignored — the transport always connects to the target set via
    // setTarget(). Keeping the signatures lets DecentScale drive us through
    // its existing connectToDevice() path with no special-case code.
    void connectToDevice(const QString& address, const QString& name) override;
    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    void disconnectFromDevice() override;
    void discoverServices() override;
    void discoverCharacteristics(const QBluetoothUuid& serviceUuid) override;
    void enableNotifications(const QBluetoothUuid& serviceUuid,
                             const QBluetoothUuid& characteristicUuid) override;
    void writeCharacteristic(const QBluetoothUuid& serviceUuid,
                             const QBluetoothUuid& characteristicUuid,
                             const QByteArray& data,
                             WriteType writeType = WriteType::WithResponse) override;
    void readCharacteristic(const QBluetoothUuid& serviceUuid,
                            const QBluetoothUuid& characteristicUuid) override;
    bool isConnected() const override;
    QString transportKind() const override { return QStringLiteral("wifi"); }

    // Decent Scale frames are exactly this many bytes; the parser scans for
    // the leading 0x03 header byte to resync if the stream ever falls out of
    // alignment. Exposed for the unit test.
    static constexpr int kDecentFrameSize = 7;
    static constexpr quint8 kDecentFrameHeader = 0x03;
    static constexpr int kDefaultConnectTimeoutMs = 2000;

    // Override the default connect timeout. Production callers don't need
    // this — the 2 s default is correct for typical home Wi-Fi. Exists so
    // unit tests can assert the timeout signal-path without making the test
    // suite multi-second-blocked on a real timeout.
    void setConnectTimeoutMs(int ms);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketErrorOccurred();
    void onConnectTimeout();

private:
    void drainFrames();
    void log(const QString& message);

    QTcpSocket* m_socket = nullptr;
    QTimer* m_connectTimeout = nullptr;
    QString m_targetIp;
    int m_targetPort = 0;
    QByteArray m_rxBuffer;
    bool m_resyncing = false;  // True while dropping leading garbage bytes; logs once per run.
};
