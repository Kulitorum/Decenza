#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "usb/usbdecentscale.h"
#include "ble/protocol/decentscaleprotocol.h"

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
};

QTEST_GUILESS_MAIN(tst_UsbDecentScale)
#include "tst_usbdecentscale.moc"
