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
        scale.startFirmwareUpdate();
        QCOMPARE(scale.writes, QList<QByteArray>{QByteArray::fromHex("031B0000000018")});

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*Half Decent Scale \\(USB\\) DISCONNECTED"));
        scale.setTestConnected(false);
    }
};

QTEST_GUILESS_MAIN(tst_UsbDecentScale)
#include "tst_usbdecentscale.moc"
