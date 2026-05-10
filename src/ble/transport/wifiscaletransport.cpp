#include "wifiscaletransport.h"

#include "../protocol/de1characteristics.h"

#include <QTcpSocket>
#include <QTimer>

WifiScaleTransport::WifiScaleTransport(QObject* parent)
    : ScaleBleTransport(parent)
    , m_socket(new QTcpSocket(this))
    , m_connectTimeout(new QTimer(this))
{
    m_connectTimeout->setSingleShot(true);
    m_connectTimeout->setInterval(kDefaultConnectTimeoutMs);

    connect(m_socket, &QTcpSocket::connected,
            this, &WifiScaleTransport::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &WifiScaleTransport::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &WifiScaleTransport::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &WifiScaleTransport::onSocketErrorOccurred);
    connect(m_connectTimeout, &QTimer::timeout,
            this, &WifiScaleTransport::onConnectTimeout);
}

WifiScaleTransport::~WifiScaleTransport() {
    if (m_socket && m_socket->state() != QTcpSocket::UnconnectedState) {
        m_socket->abort();
    }
}

void WifiScaleTransport::setTarget(const QString& ip, int port) {
    m_targetIp = ip;
    m_targetPort = port;
}

void WifiScaleTransport::setConnectTimeoutMs(int ms) {
    m_connectTimeout->setInterval(ms);
}

void WifiScaleTransport::connectToDevice(const QString& /*address*/,
                                         const QString& /*name*/) {
    if (m_targetIp.isEmpty() || m_targetPort <= 0) {
        emit error("Wi-Fi target not set; call setTarget(ip, port) first");
        return;
    }
    if (m_socket->state() != QTcpSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_rxBuffer.clear();
    m_resyncing = false;
    log(QString("Connecting to %1:%2").arg(m_targetIp).arg(m_targetPort));
    m_connectTimeout->start();
    m_socket->connectToHost(m_targetIp, static_cast<quint16>(m_targetPort));
}

void WifiScaleTransport::connectToDevice(const QBluetoothDeviceInfo& /*device*/) {
    connectToDevice(QString(), QString());
}

void WifiScaleTransport::disconnectFromDevice() {
    m_connectTimeout->stop();
    if (m_socket->state() != QTcpSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void WifiScaleTransport::discoverServices() {
    // Decent Scale service is implicit over Wi-Fi (frames arrive on a single
    // socket — there is no service discovery on the wire). Emit the
    // BLE-shaped events asynchronously so the consumer's discovery state
    // machine progresses normally.
    QTimer::singleShot(0, this, [this]() {
        emit serviceDiscovered(Scale::Decent::SERVICE);
        emit servicesDiscoveryFinished();
    });
}

void WifiScaleTransport::discoverCharacteristics(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Decent::SERVICE) return;
    QTimer::singleShot(0, this, [this, serviceUuid]() {
        emit characteristicsDiscoveryFinished(serviceUuid);
    });
}

void WifiScaleTransport::enableNotifications(const QBluetoothUuid& /*serviceUuid*/,
                                             const QBluetoothUuid& /*characteristicUuid*/) {
    // No CCCD over TCP — the firmware streams frames unconditionally to the
    // connected client. DecentScale's watchdog still calls this on stalls; a
    // no-op is the right behavior there because frames either are or aren't
    // arriving on the socket independent of any "enable" step.
}

void WifiScaleTransport::writeCharacteristic(const QBluetoothUuid& /*serviceUuid*/,
                                             const QBluetoothUuid& characteristicUuid,
                                             const QByteArray& data,
                                             WriteType /*writeType*/) {
    if (m_socket->state() != QTcpSocket::ConnectedState) {
        emit error("Cannot write: TCP socket not connected");
        return;
    }
    const qint64 written = m_socket->write(data);
    if (written != data.size()) {
        emit error(QString("Short TCP write: %1/%2 bytes").arg(written).arg(data.size()));
        return;
    }
    emit characteristicWritten(characteristicUuid);
}

void WifiScaleTransport::readCharacteristic(const QBluetoothUuid& /*serviceUuid*/,
                                            const QBluetoothUuid& /*characteristicUuid*/) {
    emit error("readCharacteristic is not supported over Wi-Fi transport");
}

bool WifiScaleTransport::isConnected() const {
    return m_socket && m_socket->state() == QTcpSocket::ConnectedState;
}

void WifiScaleTransport::onSocketConnected() {
    m_connectTimeout->stop();
    log(QString("Connected to %1:%2").arg(m_targetIp).arg(m_targetPort));
    emit connected();
}

void WifiScaleTransport::onSocketDisconnected() {
    m_connectTimeout->stop();
    m_rxBuffer.clear();
    log("Disconnected");
    emit disconnected();
}

void WifiScaleTransport::onSocketReadyRead() {
    m_rxBuffer.append(m_socket->readAll());
    drainFrames();
}

void WifiScaleTransport::onSocketErrorOccurred() {
    m_connectTimeout->stop();
    const QString reason = m_socket->errorString();
    log(QString("Socket error: %1").arg(reason));
    emit error(reason);
}

void WifiScaleTransport::onConnectTimeout() {
    if (m_socket->state() == QTcpSocket::ConnectedState) return;
    m_socket->abort();
    log(QString("Connect timeout after %1 ms").arg(m_connectTimeout->interval()));
    emit error("connect timeout");
}

void WifiScaleTransport::drainFrames() {
    while (m_rxBuffer.size() >= kDecentFrameSize) {
        if (static_cast<quint8>(m_rxBuffer.at(0)) != kDecentFrameHeader) {
            if (!m_resyncing) {
                log(QString("Frame misalignment — resyncing on 0x%1 header")
                        .arg(kDecentFrameHeader, 2, 16, QChar('0')));
                m_resyncing = true;
            }
            m_rxBuffer.remove(0, 1);
            continue;
        }
        m_resyncing = false;
        const QByteArray frame = m_rxBuffer.left(kDecentFrameSize);
        m_rxBuffer.remove(0, kDecentFrameSize);
        emit characteristicChanged(Scale::Decent::READ, frame);
    }
}

void WifiScaleTransport::log(const QString& message) {
    const QString line = QStringLiteral("[wifi/transport] ") + message;
    qDebug().noquote() << line;
    emit logMessage(line);
}
