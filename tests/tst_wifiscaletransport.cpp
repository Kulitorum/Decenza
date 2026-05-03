// Unit tests for WifiScaleTransport.
//
// Strategy: stand up a QTcpServer on localhost (OS-assigned port), point the
// transport at it, drive bytes from the server-side socket, and assert the
// transport's BLE-shaped signals fire correctly. No DecentScale instance is
// involved — these tests pin the transport-level contract only. Protocol
// parsing is covered by tst_scaleprotocol.

#include "ble/protocol/de1characteristics.h"
#include "ble/transport/wifiscaletransport.h"

#include <QByteArray>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class tst_WifiScaleTransport : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void emitsConnectedAndFramesOnAlignedStream();
    void buffersPartialFrameAcrossReads();
    void resyncsAfterMidStreamMisalignment();
    void writeCharacteristicReachesSocket();
    void connectTimeoutEmitsError();

private:
    // Build a 7-byte Decent Scale weight frame with the given grams value.
    // Header 0x03, command 0xCE, weight in decigrams big-endian, XOR
    // checksum at byte 6.
    static QByteArray makeWeightFrame(double grams);

    void waitForServerSocket();

    QTcpServer* m_server = nullptr;
    QTcpSocket* m_serverSide = nullptr;  // The socket the QTcpServer accepted.
    WifiScaleTransport* m_transport = nullptr;
};

void tst_WifiScaleTransport::init() {
    m_server = new QTcpServer(this);
    QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));
    m_transport = new WifiScaleTransport(this);
    m_transport->setTarget(QHostAddress(QHostAddress::LocalHost).toString(),
                           m_server->serverPort());
}

void tst_WifiScaleTransport::cleanup() {
    delete m_transport;
    m_transport = nullptr;
    if (m_serverSide) {
        m_serverSide->disconnectFromHost();
        m_serverSide = nullptr;
    }
    delete m_server;
    m_server = nullptr;
}

QByteArray tst_WifiScaleTransport::makeWeightFrame(double grams) {
    QByteArray f(7, '\0');
    f[0] = static_cast<char>(0x03);
    f[1] = static_cast<char>(0xCE);
    const qint16 dg = static_cast<qint16>(qRound(grams * 10.0));
    f[2] = static_cast<char>((dg >> 8) & 0xFF);
    f[3] = static_cast<char>(dg & 0xFF);
    quint8 xor8 = 0;
    for (int i = 0; i < 6; ++i) xor8 ^= static_cast<quint8>(f[i]);
    f[6] = static_cast<char>(xor8);
    return f;
}

void tst_WifiScaleTransport::waitForServerSocket() {
    QVERIFY(m_server->waitForNewConnection(2000));
    m_serverSide = m_server->nextPendingConnection();
    QVERIFY(m_serverSide != nullptr);
}

void tst_WifiScaleTransport::emitsConnectedAndFramesOnAlignedStream() {
    QSignalSpy connectedSpy(m_transport, &ScaleBleTransport::connected);
    QSignalSpy frameSpy(m_transport, &ScaleBleTransport::characteristicChanged);

    m_transport->connectToDevice(QString(), QString());
    waitForServerSocket();
    QTRY_VERIFY(connectedSpy.count() >= 1);

    const QByteArray frameA = makeWeightFrame(18.5);
    const QByteArray frameB = makeWeightFrame(36.0);
    m_serverSide->write(frameA + frameB);
    QVERIFY(m_serverSide->waitForBytesWritten(1000));

    QTRY_COMPARE(frameSpy.count(), 2);
    QCOMPARE(frameSpy.at(0).at(0).value<QBluetoothUuid>(), Scale::Decent::READ);
    QCOMPARE(frameSpy.at(0).at(1).toByteArray(), frameA);
    QCOMPARE(frameSpy.at(1).at(1).toByteArray(), frameB);
}

void tst_WifiScaleTransport::buffersPartialFrameAcrossReads() {
    QSignalSpy connectedSpy(m_transport, &ScaleBleTransport::connected);
    QSignalSpy frameSpy(m_transport, &ScaleBleTransport::characteristicChanged);

    m_transport->connectToDevice(QString(), QString());
    waitForServerSocket();
    QTRY_VERIFY(connectedSpy.count() >= 1);

    const QByteArray frame = makeWeightFrame(12.3);

    // First write: 4 of 7 bytes. No frame should be emitted yet.
    m_serverSide->write(frame.left(4));
    QVERIFY(m_serverSide->waitForBytesWritten(1000));
    QTest::qWait(50);
    QCOMPARE(frameSpy.count(), 0);

    // Second write: remaining 3 bytes. Now exactly one frame is delivered.
    m_serverSide->write(frame.mid(4));
    QVERIFY(m_serverSide->waitForBytesWritten(1000));

    QTRY_COMPARE(frameSpy.count(), 1);
    QCOMPARE(frameSpy.at(0).at(1).toByteArray(), frame);
}

void tst_WifiScaleTransport::resyncsAfterMidStreamMisalignment() {
    QSignalSpy connectedSpy(m_transport, &ScaleBleTransport::connected);
    QSignalSpy frameSpy(m_transport, &ScaleBleTransport::characteristicChanged);

    m_transport->connectToDevice(QString(), QString());
    waitForServerSocket();
    QTRY_VERIFY(connectedSpy.count() >= 1);

    const QByteArray frameA = makeWeightFrame(7.7);
    const QByteArray frameB = makeWeightFrame(14.0);

    QByteArray garbage;
    garbage.append(static_cast<char>(0xFF));
    garbage.append(static_cast<char>(0xFF));

    m_serverSide->write(garbage + frameA + frameB);
    QVERIFY(m_serverSide->waitForBytesWritten(1000));

    QTRY_COMPARE(frameSpy.count(), 2);
    QCOMPARE(frameSpy.at(0).at(1).toByteArray(), frameA);
    QCOMPARE(frameSpy.at(1).at(1).toByteArray(), frameB);
}

void tst_WifiScaleTransport::writeCharacteristicReachesSocket() {
    QSignalSpy connectedSpy(m_transport, &ScaleBleTransport::connected);
    QSignalSpy writtenSpy(m_transport, &ScaleBleTransport::characteristicWritten);

    m_transport->connectToDevice(QString(), QString());
    waitForServerSocket();
    QTRY_VERIFY(connectedSpy.count() >= 1);

    // Tare frame: 03 0F 01 00 00 00 [xor]. Bytes are checked at the server,
    // not parsed — the transport's job is byte-perfect passthrough.
    QByteArray tare(7, '\0');
    tare[0] = 0x03; tare[1] = 0x0F; tare[2] = 0x01;
    quint8 xor8 = 0;
    for (int i = 0; i < 6; ++i) xor8 ^= static_cast<quint8>(tare[i]);
    tare[6] = static_cast<char>(xor8);

    m_transport->writeCharacteristic(Scale::Decent::SERVICE,
                                     Scale::Decent::WRITE,
                                     tare);
    QCOMPARE(writtenSpy.count(), 1);

    // Poll the event loop rather than waitForReadyRead — the latter is
    // documented as flaky on Windows. QTRY_VERIFY pumps events until the
    // expected bytes arrive on the server-side socket.
    QByteArray received;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            received += m_serverSide->readAll();
            return received.size() >= tare.size();
        }(),
        1000);
    QCOMPARE(received, tare);
}

void tst_WifiScaleTransport::connectTimeoutEmitsError() {
    // Re-target at a port nothing is listening on. Closing m_server
    // releases its port; the OS may or may not reuse it for the connect
    // attempt — that's fine, what matters is that no listener accepts.
    const quint16 deadPort = m_server->serverPort();
    m_server->close();

    auto* transport = new WifiScaleTransport(this);
    transport->setTarget(QHostAddress(QHostAddress::LocalHost).toString(), deadPort);
    transport->setConnectTimeoutMs(150);

    QSignalSpy errorSpy(transport, &ScaleBleTransport::error);
    transport->connectToDevice(QString(), QString());

    // Either the OS refuses the connect immediately (most platforms — port
    // is genuinely closed) or our 150 ms timer fires. Both paths land on
    // the same `error()` signal — the spec scenario is "connect that
    // doesn't reach the connected state SHALL emit error()", and that's
    // what we're pinning here.
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 2000);
    QVERIFY(!transport->isConnected());

    delete transport;
}

QTEST_MAIN(tst_WifiScaleTransport)
#include "tst_wifiscaletransport.moc"
