#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QFile>

#include "usb/usbdecentscale.h"
#include "usb/usbhotplug.h"
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

    // --- USB hotplug dispatch ------------------------------------------------
    //
    // device_filter.xml is the single list of ids the app supports; these predicates
    // decide which manager(s) an event reaches.
    //
    // The ids do NOT partition. The Half Decent Scale ships with a CH9102 (0x55D3),
    // the same bridge chip the DE1 uses, so that id must reach both managers and the
    // protocol probes decide. An earlier version routed 0x55D3 to the DE1 alone; on
    // hardware an HDS then timed out on the DE1's <+M> probe and never reached the
    // scale manager at all.
    //
    // EXPECTATIONS ARE LITERALS, DELIBERATELY. Comparing against the kUsb* constants
    // moves both sides at once: with one changed to 0x9999 the suite stayed green.
    // Do not "tidy" these into the constants.
private:
    // One table drives both slots. Two independent lists made the pair satisfiable
    // by the careless repair it exists to prevent — add an id to the XML, append it
    // to the other slot's list, green, with no routing row.
    struct SupportedId { int pid; bool mayBeScale; bool mayBeDe1; const char* name; };
    static QList<SupportedId> supportedIds() {
        return {
            {0x7522, true,  false, "scale CH340 0x7522"},
            {0x7523, true,  false, "scale CH340 0x7523"},
            {0x55D3, true,  true,  "shared CH9102 0x55D3 (DE1 and HDS)"},
        };
    }

private slots:
    void deviceFilterIdsRouteToTheRightManager_data() {
        QTest::addColumn<int>("productId");
        QTest::addColumn<bool>("mayBeScale");
        QTest::addColumn<bool>("mayBeDe1");
        for (const auto& id : supportedIds())
            QTest::newRow(id.name) << id.pid << id.mayBeScale << id.mayBeDe1;
    }

    void deviceFilterIdsRouteToTheRightManager() {
        QFETCH(int, productId);
        QFETCH(bool, mayBeScale);
        QFETCH(bool, mayBeDe1);
        QCOMPARE(usbPidMayBeScale(productId), mayBeScale);
        QCOMPARE(usbPidMayBeDe1(productId), mayBeDe1);
        // Every supported id must reach at least one manager, or its events vanish.
        QVERIFY(mayBeScale || mayBeDe1);
    }

    // The rows above are only meaningful while they ARE the supported ids. This
    // binds them to device_filter.xml, so an id added there without a row here —
    // or a row here for an id the app no longer declares — fails rather than
    // quietly reducing what the table covers.
    void theDeviceFilterListsExactlyTheIdsCoveredAbove() {
        const QString path = QStringLiteral("%1/android/res/xml/device_filter.xml")
                                 .arg(QStringLiteral(DECENZA_SOURCE_DIR));
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("cannot open %1").arg(path)));

        QRegularExpression re(QStringLiteral("product-id=\"(\\d+)\""));
        auto it = re.globalMatch(QString::fromUtf8(f.readAll()));
        QList<int> found;
        while (it.hasNext()) found.append(it.next().captured(1).toInt());
        std::sort(found.begin(), found.end());

        // From the routing table, not a second literal list — that is what makes an
        // id added to the XML reach the predicates rather than stopping here.
        QList<int> expected;
        for (const auto& id : supportedIds()) expected.append(id.pid);
        std::sort(expected.begin(), expected.end());
        QCOMPARE(found, expected);
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
