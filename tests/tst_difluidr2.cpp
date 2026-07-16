#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "ble/refractometers/difluidr2.h"
#include "ble/protocol/de1characteristics.h"

// Test DiFluid R2 refractometer BLE packet parsing, checksum validation,
// and device name matching.
//
// Protocol: header 0xDF 0xDF, func, cmd, datalen, data, additive checksum.
// Func 3 = Device Action. Pack 0 = status, Pack 1 = temperature, Pack 2 = TDS, Pack 3 = average TDS.

class tst_DiFluidR2 : public QObject {
    Q_OBJECT

private:
    // Build an R2 packet with valid additive checksum
    // Protocol: DF DF <func> <cmd> <datalen> <data...> <checksum>
    // Checksum = sum of all preceding bytes (0 to N-2 in final packet), mod 256
    static QByteArray buildR2Packet(uint8_t func, uint8_t cmd, const QByteArray& data) {
        QByteArray pkt;
        pkt.append(static_cast<char>(0xDF));  // Header
        pkt.append(static_cast<char>(0xDF));  // Header
        pkt.append(static_cast<char>(func));  // Function
        pkt.append(static_cast<char>(cmd));   // Command
        pkt.append(static_cast<char>(data.size()));  // DataLen

        pkt.append(data);

        // Additive checksum of all bytes so far (before appending checksum byte)
        uint8_t checksum = 0;
        for (qsizetype i = 0; i < pkt.size(); ++i) {
            checksum += static_cast<uint8_t>(pkt[i]);
        }
        pkt.append(static_cast<char>(checksum));
        return pkt;
    }

    // Build a TDS packet: Func=3, Cmd=0, PackNo=2, TDS raw = tds * 100
    static QByteArray buildTdsPacket(double tds) {
        uint16_t raw = static_cast<uint16_t>(qRound(tds * 100.0));
        QByteArray data;
        data.append(static_cast<char>(0x02));  // PackNo = 2 (TDS result)
        data.append(static_cast<char>((raw >> 8) & 0xFF));
        data.append(static_cast<char>(raw & 0xFF));
        return buildR2Packet(0x03, 0x00, data);
    }

    // Build an average TDS packet: Func=3, Cmd=0, PackNo=3, TDS raw = tds * 100
    static QByteArray buildAverageTdsPacket(double tds) {
        uint16_t raw = static_cast<uint16_t>(qRound(tds * 100.0));
        QByteArray data;
        data.append(static_cast<char>(0x03));  // PackNo = 3 (average TDS result)
        data.append(static_cast<char>((raw >> 8) & 0xFF));
        data.append(static_cast<char>(raw & 0xFF));
        return buildR2Packet(0x03, 0x00, data);
    }

    // Build a full TDS packet incl. refractive index: PackNo=2,
    // Data1-2 = tds*100, Data3-6 = ri*100000 (big-endian)
    static QByteArray buildTdsPacketWithRi(double tds, double ri) {
        uint16_t tdsRaw = static_cast<uint16_t>(qRound(tds * 100.0));
        uint32_t riRaw = static_cast<uint32_t>(qRound(ri * 100000.0));
        QByteArray data;
        data.append(static_cast<char>(0x02));  // PackNo = 2 (TDS result)
        data.append(static_cast<char>((tdsRaw >> 8) & 0xFF));
        data.append(static_cast<char>(tdsRaw & 0xFF));
        data.append(static_cast<char>((riRaw >> 24) & 0xFF));
        data.append(static_cast<char>((riRaw >> 16) & 0xFF));
        data.append(static_cast<char>((riRaw >> 8) & 0xFF));
        data.append(static_cast<char>(riRaw & 0xFF));
        return buildR2Packet(0x03, 0x00, data);
    }

    // Build a device-model response: Func=0, Cmd=1, Data = ASCII model string
    static QByteArray buildDeviceModelPacket(const QByteArray& model) {
        return buildR2Packet(0x00, 0x01, model);
    }

    // Build a temperature packet: Func=3, Cmd=0, PackNo=1
    // Prism temp = tempC * 10, tank temp = tempC * 10
    static QByteArray buildTemperaturePacket(double tempC) {
        uint16_t raw = static_cast<uint16_t>(qRound(tempC * 10.0));
        QByteArray data;
        data.append(static_cast<char>(0x01));  // PackNo = 1 (temperature)
        data.append(static_cast<char>((raw >> 8) & 0xFF));  // Prism temp high
        data.append(static_cast<char>(raw & 0xFF));          // Prism temp low
        data.append(static_cast<char>((raw >> 8) & 0xFF));  // Tank temp high
        data.append(static_cast<char>(raw & 0xFF));          // Tank temp low
        return buildR2Packet(0x03, 0x00, data);
    }

    // Build a status packet: Func=3, Cmd=0, PackNo=0, Data1=status
    static QByteArray buildStatusPacket(uint8_t status) {
        QByteArray data;
        data.append(static_cast<char>(0x00));  // PackNo = 0 (status)
        data.append(static_cast<char>(status));
        return buildR2Packet(0x03, 0x00, data);
    }

    // Build an error response: Func=3, Cmd=254, Data=errClass+errCode
    static QByteArray buildErrorPacket(uint8_t errClass, uint8_t errCode) {
        QByteArray data;
        data.append(static_cast<char>(errClass));
        data.append(static_cast<char>(errCode));
        return buildR2Packet(0x03, 0xFE, data);
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // === Device name matching ===

    void isR2DeviceMatchesR2Extract() {
        QVERIFY(DiFluidR2::isR2Device("R2 Extract"));
        QVERIFY(DiFluidR2::isR2Device("r2 extract"));
        QVERIFY(DiFluidR2::isR2Device("R2Extract"));
    }

    void isR2DeviceMatchesDiFluidR2() {
        QVERIFY(DiFluidR2::isR2Device("DiFluid R2"));
        QVERIFY(DiFluidR2::isR2Device("difluid r2"));
        QVERIFY(DiFluidR2::isR2Device("DIFLUID R2 EXTRACT"));
    }

    void isR2DeviceRejectsMicrobalance() {
        // DiFluid Microbalance is a scale, not an R2
        QVERIFY(!DiFluidR2::isR2Device("DiFluid"));
        QVERIFY(!DiFluidR2::isR2Device("difluid"));
        QVERIFY(!DiFluidR2::isR2Device("Microbalance"));
        QVERIFY(!DiFluidR2::isR2Device("DiFluid Microbalance"));
    }

    void isR2DeviceRejectsOtherDevices() {
        QVERIFY(!DiFluidR2::isR2Device("Acaia Lunar"));
        QVERIFY(!DiFluidR2::isR2Device("Decent Scale"));
        QVERIFY(!DiFluidR2::isR2Device(""));
    }

    // === Checksum validation ===

    void checksumValidForCorrectPacket() {
        DiFluidR2 r2(nullptr);
        QByteArray pkt = buildTdsPacket(8.50);
        QVERIFY(r2.validateChecksum(pkt));
    }

    void checksumInvalidForCorruptedPacket() {
        DiFluidR2 r2(nullptr);
        QByteArray pkt = buildTdsPacket(8.50);
        // Corrupt one data byte
        pkt[5] = static_cast<char>(static_cast<uint8_t>(pkt[5]) ^ 0xFF);
        QVERIFY(!r2.validateChecksum(pkt));
    }

    void checksumInvalidForShortPacket() {
        DiFluidR2 r2(nullptr);
        QByteArray pkt;
        pkt.append(static_cast<char>(0xDF));
        pkt.append(static_cast<char>(0xDF));
        QVERIFY(!r2.validateChecksum(pkt));
    }

    void checksumValidForMinimumPacket() {
        // 6-byte packet with dataLen=0 should pass validation
        DiFluidR2 r2(nullptr);
        QByteArray pkt = buildR2Packet(0x01, 0x00, QByteArray());
        QCOMPARE(pkt.size(), 6);
        QVERIFY(r2.validateChecksum(pkt));
    }

    // === TDS packet parsing ===

    void parseTdsPacketEmitsSignal() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::tdsChanged);

        QByteArray pkt = buildTdsPacket(8.50);
        r2.handlePacket(pkt);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 8.50);
        QCOMPARE(r2.tds(), 8.50);
    }

    void parseTdsPacketEmitsMeasurementComplete() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::measurementComplete);

        r2.handlePacket(buildTdsPacket(10.25));

        QCOMPARE(spy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    void parseTdsPacketZero() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::tdsChanged);

        r2.handlePacket(buildTdsPacket(0.0));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(r2.tds(), 0.0);
    }

    void parseTdsPacketHighValue() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::tdsChanged);

        r2.handlePacket(buildTdsPacket(15.75));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 15.75);
    }

    // === Average TDS packet parsing (pack 3) ===

    void parseAverageTdsPacket() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);

        r2.handlePacket(buildAverageTdsPacket(9.25));

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(tdsSpy.at(0).at(0).toDouble(), 9.25);
        QCOMPARE(r2.tds(), 9.25);
        QCOMPARE(completeSpy.count(), 1);
    }

    // === Instrumentation: refractive index + device model (Brix-vs-TDS diagnosis) ===

    // A full pack-2 with the refractive-index sub-field still parses TDS correctly,
    // and the RI is logged for cross-checking whether concentration is TDS or Brix.
    void parseTdsPacketWithRefractiveIndex() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);

        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("Refractive index: 1\\.35520 \\(raw=135520\\)"));
        r2.handlePacket(buildTdsPacketWithRi(8.50, 1.35520));

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(tdsSpy.at(0).at(0).toDouble(), 8.50);
        QCOMPARE(r2.tds(), 8.50);
    }

    // Genuine R2 Extract reports model "DFT-R102" — logged as TDS-bearing.
    void parseDeviceModelGenuineExtract() {
        DiFluidR2 r2(nullptr);
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("Device model: \"DFT-R102\".*genuine R2 Extract"));
        r2.handlePacket(buildDeviceModelPacket("DFT-R102"));
    }

    // Any other model (Brix variant / rebrand / clone) is flagged so a Brix-as-TDS
    // reading is diagnosable from the log.
    void parseDeviceModelNonExtractFlagged() {
        DiFluidR2 r2(nullptr);
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression("Device model: \"ATOM-R2X\".*NOT a standard R2 Extract"));
        r2.handlePacket(buildDeviceModelPacket("ATOM-R2X"));
    }

    // === Out-of-range sentinel rejection (regression) ===
    //
    // Field incident: a failed R2 measurement put raw 0xFFE5 (65509 → 655.09%)
    // in the TDS field one packet before an `R2 error class=0 code=2` storm.
    // The well-formed-but-impossible value passed the checksum, was emitted as
    // a real reading, and got autosaved onto the shot (EY 1342.9%). It must
    // never reach a consumer. Pack 2 is shared by the app "Read TDS" button
    // (single test) and the physical R2 Start button, so one gate covers both.

    void rejectsImplausiblyHighTds() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("TDS out of range.*raw=65509"));
        r2.handlePacket(buildTdsPacket(655.09));  // raw = 65509 = 0xFFE5

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(completeSpy.count(), 0);
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
        QCOMPARE(r2.tds(), 0.0);  // m_tds left untouched, no garbage retained
    }

    void rejectsImplausiblyHighAverageTds() {
        // Same gate must apply to the averaged result (pack 3).
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Average TDS out of range"));
        r2.handlePacket(buildAverageTdsPacket(655.09));

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(completeSpy.count(), 0);
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    void acceptsTdsAtTopOfDeviceRange() {
        // 30% is unusually strong but within the R2's physical range — the
        // guard must not reject real (if rare) readings.
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);

        r2.handlePacket(buildTdsPacket(30.00));

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(tdsSpy.at(0).at(0).toDouble(), 30.00);
        QCOMPARE(r2.tds(), 30.00);
    }

    // === Temperature packet parsing ===

    void parseTemperaturePacket() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::temperatureChanged);

        r2.handlePacket(buildTemperaturePacket(23.50));

        QCOMPARE(spy.count(), 1);
        // Temperature is prism temp / 10.0, encoded as tempC * 10
        QCOMPARE(spy.at(0).at(0).toDouble(), 23.50);
        QCOMPARE(r2.temperature(), 23.50);
    }

    // === Status packet parsing ===

    void parseStatusFinished() {
        DiFluidR2 r2(nullptr);
        // Status 0 = "Test finished" — no signals emitted, just logged
        r2.handlePacket(buildStatusPacket(0x00));
        // No crash, no error
    }

    void parseStatusStarted() {
        DiFluidR2 r2(nullptr);
        // Status 11 = "Test started" — no signals emitted, just logged
        r2.handlePacket(buildStatusPacket(0x0B));
        // No crash, no error
    }

    // === Error response parsing (Func=3, Cmd=254) ===

    void parseErrorNoLiquid() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 error: class=2 code=3"));
        r2.handlePacket(buildErrorPacket(2, 3));  // errClass=2, errCode=3 = no liquid

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains("liquid"));
    }

    void parseErrorBeyondRange() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 error: class=2 code=4"));
        r2.handlePacket(buildErrorPacket(2, 4));  // errClass=2, errCode=4 = beyond range

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains("range"));
    }

    void parseErrorUnknown() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
        QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);

        // Cmd=255 = unknown error: non-actionable, so it is logged but NOT
        // surfaced to the UI (errorOccurred is wired to the error dialog —
        // surfacing unknown/benign device errors spams it). Measuring state is
        // still cleared so the UI doesn't hang.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 unknown error"));
        r2.handlePacket(buildR2Packet(0x03, 0xFF, QByteArray()));

        QCOMPARE(errorSpy.count(), 0);
        QCOMPARE(measSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    void parseErrorNonActionableNotSurfaced() {
        // Regression: the R2 emits a benign `class=0 code=2` error around a
        // SUCCESSFUL read. It carries no useful info (the data already arrived),
        // so it must be logged but NOT surfaced — before this gate it spammed
        // the error dialog once errorOccurred was wired to the UI. Only the
        // class-2 measurement failures (no-liquid / beyond-range) surface.
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
        QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 error: class=0 code=2"));
        r2.handlePacket(buildErrorPacket(0, 2));

        QCOMPARE(errorSpy.count(), 0);
        // Not surfaced, but the measuring state is still cleared so the UI
        // doesn't hang on a benign status error.
        QCOMPARE(measSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    // === Invalid packets ===

    void rejectsShortPacket() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy tempSpy(&r2, &DiFluidR2::temperatureChanged);

        QByteArray shortPkt;
        shortPkt.append(static_cast<char>(0xDF));
        shortPkt.append(static_cast<char>(0xDF));
        r2.handlePacket(shortPkt);

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(tempSpy.count(), 0);
    }

    void rejectsWrongHeader() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);

        QByteArray pkt = buildTdsPacket(8.50);
        pkt[0] = 0x00;  // Wrong header
        r2.handlePacket(pkt);

        QCOMPARE(tdsSpy.count(), 0);
    }

    void rejectsBadChecksum() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);

        QByteArray pkt = buildTdsPacket(8.50);
        // Corrupt the checksum
        pkt[pkt.size() - 1] = static_cast<char>(static_cast<uint8_t>(pkt[pkt.size() - 1]) + 1);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Checksum failed"));
        r2.handlePacket(pkt);

        QCOMPARE(tdsSpy.count(), 0);
    }

    // === UUIDs ===

    void serviceUuidIsCorrect() {
        QCOMPARE(Refractometer::DiFluidR2::SERVICE.toString(),
                 QString("{000000ff-0000-1000-8000-00805f9b34fb}"));
    }

    void characteristicUuidIsCorrect() {
        QCOMPARE(Refractometer::DiFluidR2::CHARACTERISTIC.toString(),
                 QString("{0000aa01-0000-1000-8000-00805f9b34fb}"));
    }
};

QTEST_GUILESS_MAIN(tst_DiFluidR2)
#include "tst_difluidr2.moc"
