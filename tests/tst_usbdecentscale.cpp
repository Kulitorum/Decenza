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
    // device_filter.xml is the single list of USB ids the app supports: the
    // manifest's launch intent-filter uses it, and the hotplug receiver filters
    // against it. usbDeviceKindForPid() then decides which manager an event goes
    // to, from the product id alone.
    //
    // The defect shape no other test catches: a scale product id present in the XML
    // but not in the C++ scale list. Nothing fails — the id is still "supported"
    // and the receiver still forwards it — but every event for that scale routes to
    // the DE1 manager, which probes for a DE1, finds none, and does nothing. A
    // silent dead scale, from an edit that looked complete.
    //
    // THE EXPECTED IDS ARE LITERALS HERE, DELIBERATELY. The first version of this
    // test compared usbDeviceKindForPid() against kUsbScalePid1/kUsbScalePid2 —
    // the same constants the function itself reads — so changing a constant moved
    // both sides and the test passed. Verified by breaking it: with
    // kUsbScalePid2 set to 0x9999 the whole suite still went green. A test that
    // cannot fail is a comment that compiles; these literals are what give it teeth.
private:
    // ONE table drives both slots below. It was two independent literal lists, and
    // that made the pair satisfiable by the careless repair it exists to prevent:
    // add a scale id to device_filter.xml, watch the XML slot go red, append the id
    // to that slot's own `expected` list — green, with no routing row and no C++
    // change, and the new scale silently routed to the DE1 manager. Now the only
    // repair is a row here, and a row must state a kind.
    struct SupportedId { int pid; UsbDeviceKind kind; const char* name; };
    static QList<SupportedId> supportedIds() {
        return {
            {0x7522, UsbDeviceKind::Scale, "scale CH340 0x7522"},
            {0x7523, UsbDeviceKind::Scale, "scale CH340 0x7523"},
            {0x55D3, UsbDeviceKind::De1,   "DE1 CH9102 0x55D3"},
        };
    }

private slots:
    void deviceFilterIdsRouteToTheRightManager_data() {
        QTest::addColumn<int>("productId");
        QTest::addColumn<int>("expectedKind");
        for (const auto& id : supportedIds())
            QTest::newRow(id.name) << id.pid << int(id.kind);
    }

    void deviceFilterIdsRouteToTheRightManager() {
        QFETCH(int, productId);
        QFETCH(int, expectedKind);
        QCOMPARE(int(usbDeviceKindForPid(productId)), expectedKind);
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
        // id added to the XML reach usbDeviceKindForPid() rather than stopping here.
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
