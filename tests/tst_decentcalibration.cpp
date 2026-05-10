// Pin the calibration command's wire bytes against the format coordinated
// with the DecenzaScale firmware repo:
//
//   0x03 0x10 [int16BE decigrams] 0x00 0x00 [xor]
//
// If this test ever fails, either the firmware contract changed (update
// firmware-coordination-prompt.md and the spec) or DecentScale::sendCommand
// got refactored in a way that breaks the byte layout. Either way, a
// silent regression in the calibration wire format would result in the
// scale storing the WRONG scale factor and weighing wildly off — a
// user-impacting bug that's invisible until the next time they tare.

#include "ble/protocol/de1characteristics.h"
#include "ble/protocol/decentscaleprotocol.h"
#include "ble/scales/decentscale.h"
#include "ble/transport/scalebletransport.h"

#include <QBluetoothUuid>
#include <QByteArray>
#include <QTest>

class WriteCapturingTransport : public ScaleBleTransport {
    Q_OBJECT
public:
    explicit WriteCapturingTransport(QObject* parent = nullptr)
        : ScaleBleTransport(parent) {}

    void connectToDevice(const QString&, const QString&) override {}
    void disconnectFromDevice() override {}
    void discoverServices() override {}
    void discoverCharacteristics(const QBluetoothUuid&) override {}
    void enableNotifications(const QBluetoothUuid&, const QBluetoothUuid&) override {}
    void writeCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&,
                             const QByteArray& data, WriteType = WriteType::WithResponse) override {
        m_writes.append(data);
    }
    void readCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&) override {}
    bool isConnected() const override { return true; }

    QList<QByteArray> m_writes;
};

class tst_DecentCalibration : public QObject {
    Q_OBJECT

private slots:
    void calibrationFrameMatchesFirmwareFormat();
    void calibrationFrameRoundTripsKnownWeights_data();
    void calibrationFrameRoundTripsKnownWeights();
    void outOfRangeGramsAreRejected();
};

namespace {
// Drive a calibration write end-to-end and return the calibration frame
// (cmd byte 0x10), discarding any wake-sequence writes (heartbeat 0x0A,
// LCD enable, etc.) that the protocol consumer emits before our
// calibrate call lands.
QByteArray captureCalibrationFrame(double grams) {
    auto* transport = new WriteCapturingTransport();
    DecentScale scale(transport);
    // Synthesize discovery completion so DecentScale::sendCommand's
    // m_characteristicsReady gate opens.
    emit transport->characteristicsDiscoveryFinished(Scale::Decent::SERVICE);
    scale.calibrateToKnownWeight(grams);
    for (const QByteArray& w : transport->m_writes) {
        if (w.size() >= 2 && static_cast<quint8>(w.at(1)) == 0x10) {
            return w;
        }
    }
    return QByteArray();
}
} // namespace

void tst_DecentCalibration::calibrationFrameMatchesFirmwareFormat() {
    const QByteArray frame = captureCalibrationFrame(100.0);
    QVERIFY2(!frame.isEmpty(), "No calibration frame written");
    QCOMPARE(frame.size(), 7);
    QCOMPARE(static_cast<quint8>(frame.at(0)), quint8{0x03});  // model header
    QCOMPARE(static_cast<quint8>(frame.at(1)), quint8{0x10});  // calibrate command
    // Weight in decigrams big-endian: 100.0 g → 1000 dg → 0x03E8
    QCOMPARE(static_cast<quint8>(frame.at(2)), quint8{0x03});
    QCOMPARE(static_cast<quint8>(frame.at(3)), quint8{0xE8});
    QCOMPARE(static_cast<quint8>(frame.at(4)), quint8{0x00});
    QCOMPARE(static_cast<quint8>(frame.at(5)), quint8{0x00});
    // XOR checksum: same algorithm as every other Decent Scale write.
    quint8 xor8 = 0;
    for (int i = 0; i < 6; ++i) xor8 ^= static_cast<quint8>(frame.at(i));
    QCOMPARE(static_cast<quint8>(frame.at(6)), xor8);
}

void tst_DecentCalibration::calibrationFrameRoundTripsKnownWeights_data() {
    QTest::addColumn<double>("grams");
    QTest::addColumn<int>("expectedDecigrams");
    QTest::newRow("typical 100 g")    << 100.0  << 1000;
    QTest::newRow("idiosyncratic 132") << 132.0 << 1320;
    QTest::newRow("under 1 g")        << 0.5    << 5;
    // Upper bound is int16-decigrams: 32767 dg = 3276.7 g. Anything higher
    // would silently overflow into a negative int16 on the wire.
    QTest::newRow("near upper bound") << 3000.0 << 30000;
}

void tst_DecentCalibration::calibrationFrameRoundTripsKnownWeights() {
    QFETCH(double, grams);
    QFETCH(int, expectedDecigrams);

    const QByteArray frame = captureCalibrationFrame(grams);
    QVERIFY(!frame.isEmpty());
    const int decoded = (static_cast<quint8>(frame.at(2)) << 8)
                       | static_cast<quint8>(frame.at(3));
    // For values up to 32767 dg the int16BE encoding is a straight unsigned
    // round-trip; only signed-overflow weights would diverge, and those are
    // rejected by the input validator.
    QCOMPARE(decoded, expectedDecigrams);
}

void tst_DecentCalibration::outOfRangeGramsAreRejected() {
    // Negative and zero — caller validation should drop both.
    QVERIFY(captureCalibrationFrame(-1.0).isEmpty());
    QVERIFY(captureCalibrationFrame(0.0).isEmpty());
    // Above the int16-decigrams ceiling (3276.7 g) — would silently
    // overflow into a negative int16 if the validator missed it.
    QVERIFY(captureCalibrationFrame(5000.0).isEmpty());
    QVERIFY(captureCalibrationFrame(20000.0).isEmpty());
}

QTEST_MAIN(tst_DecentCalibration)
#include "tst_decentcalibration.moc"
