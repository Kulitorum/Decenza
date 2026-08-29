#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "usb/usbdecentscale.h"

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
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Starting firmware update to 3\\.1\\.14.*"));
        scale.startFirmwareUpdate(QStringLiteral("3.1.14"));
        // USB is framed rather than packetised: a targeted 0x1B is exactly five
        // bytes, so nothing is padded and nothing spills to the scale's text
        // path (openscale decentCommandFrameLength).
        QCOMPARE(scale.writes, QList<QByteArray>{QByteArray::fromHex("031B83818E")});

        scale.writes.clear();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*unparsable target version.*"));
        scale.startFirmwareUpdate(QStringLiteral("3.1"));
        QVERIFY(scale.writes.isEmpty());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Half Decent Scale \\(USB\\) DISCONNECTED"));
        scale.setTestConnected(false);
    }
};

QTEST_GUILESS_MAIN(tst_UsbDecentScale)
#include "tst_usbdecentscale.moc"
