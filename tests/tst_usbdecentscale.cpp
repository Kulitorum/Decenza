#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "usb/usbdecentscale.h"
#include "ble/protocol/decentscaleprotocol.h"
#include "messagecapture.h"

class RecordingUsbDecentScale : public UsbDecentScale {
public:
    using UsbDecentScale::UsbDecentScale;

    void setTestConnected(bool connected) { setConnected(connected); }
    QList<QByteArray> writes;

protected:
    void writeRaw(const QByteArray& data) override { writes.append(data); }
};

class tst_UsbDecentScale : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void hdsFirmwareVersionAndUpdateCommand() {
        RecordingUsbDecentScale scale;
        QSignalSpy versionSpy(&scale, &ScaleDevice::firmwareVersionChanged);
        const QByteArray response = QByteArray::fromHex("030A000032031D");
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: 3\\.1\\.13 \\(raw 0x03 0x1d\\).*"));
        scale.processPacket(response);

        QCOMPARE(scale.firmwareVersion(), QStringLiteral("3.1.13"));
        QVERIFY(scale.supportsFirmwareUpdate());
        QCOMPARE(versionSpy.count(), 1);

        scale.setTestConnected(true);
        scale.writes.clear();
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QRegularExpression::escape(
            DecentScaleProtocol::firmwareUpdateStartingMessage(QStringLiteral("3.1.14")))));
        scale.startFirmwareUpdate(QStringLiteral("3.1.14"));
        // USB is framed rather than packetised: a targeted 0x1B is exactly five
        // bytes, so nothing is padded and nothing spills to the scale's text
        // path (openscale decentCommandFrameLength).
        QCOMPARE(scale.writes, QList<QByteArray>{QByteArray::fromHex("031B83818E")});

        scale.writes.clear();
        // One bad value; the accepted set is asserted by
        // tst_scaleprotocol's hdsTargetVersionEncoding table. This proves only
        // that the driver consults that predicate and sends nothing.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QRegularExpression::escape(
            DecentScaleProtocol::firmwareUpdateBadTargetMessage(QStringLiteral("3.1.x")))));
        scale.startFirmwareUpdate(QStringLiteral("3.1.x"));
        QVERIFY(scale.writes.isEmpty());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Half Decent Scale \\(USB\\) DISCONNECTED"));
        scale.setTestConnected(false);
    }

    void adsDebugFrameIsConsumedWholeNotResyncedThrough() {
        // The 41-byte ADS debug frame (openscale include/usbcomm.h
        // buildAdsDebugPacket) is not a 7-byte packet. Byte-at-a-time resync
        // walked through it, and every 7-byte window it tried was a chance to
        // find a checksum that happened to match. This frame carries such a
        // window on purpose: bytes 8-14 are a well-formed 50.0 g weight packet,
        // which the old framer would have emitted as a real weighing.
        RecordingUsbDecentScale scale;
        QSignalSpy spy(&scale, &ScaleDevice::weightChanged);
        MessageCapture capture;

        QByteArray frame(DecentScaleProtocol::AdsDebugFrameLength, 0);
        frame[0] = 0x03;
        frame[1] = static_cast<char>(DecentScaleProtocol::TypeAdsDebug);
        const QByteArray planted = buildUsbWeightPacket(50.0);
        frame.replace(8, planted.size(), planted);
        uint8_t xorSum = 0;
        for (int i = 0; i < DecentScaleProtocol::AdsDebugFrameLength - 1; i++)
            xorSum ^= static_cast<uint8_t>(frame[i]);
        frame[DecentScaleProtocol::AdsDebugFrameLength - 1] = static_cast<char>(xorSum);

        scale.m_buffer = frame;
        scale.processBuffer();

        QCOMPARE(spy.count(), 0);
        QVERIFY(scale.m_buffer.isEmpty());
        MessageCapture::Entry entry;
        QVERIFY(capture.single(QStringLiteral("ADS debug frame"), &entry));
        QVERIFY(entry.text.contains(QStringLiteral("03 25")));

        // The framer still frames: a real packet arriving next is parsed.
        scale.m_buffer = buildUsbWeightPacket(42.0);
        scale.processBuffer();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toDouble(), 42.0);
    }

private:
    static QByteArray buildUsbWeightPacket(double grams) {
        const int16_t raw = static_cast<int16_t>(qRound(grams * 10.0));
        QByteArray pkt(DecentScaleProtocol::StandardFrameLength, 0);
        pkt[0] = 0x03;
        pkt[1] = static_cast<char>(0xCE);
        pkt[2] = static_cast<char>((raw >> 8) & 0xFF);
        pkt[3] = static_cast<char>(raw & 0xFF);
        pkt[6] = static_cast<char>(DecentScaleProtocol::calculateXor(pkt));
        return pkt;
    }
};

QTEST_GUILESS_MAIN(tst_UsbDecentScale)
#include "tst_usbdecentscale.moc"
