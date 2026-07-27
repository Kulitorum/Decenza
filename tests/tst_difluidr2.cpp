#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "ble/refractometers/difluidr2.h"
#include "ble/protocol/de1characteristics.h"
#include "ble/transport/scalebletransport.h"

// Test DiFluid R2 refractometer BLE packet parsing, checksum validation,
// and device name matching.
//
// Protocol: header 0xDF 0xDF, func, cmd, datalen, data, additive checksum.
// Func 3 = Device Action. Pack 0 = status, Pack 1 = temperature, Pack 2 = TDS, Pack 3 = average TDS.

// A refractometer that implements nothing beyond the pure virtuals, standing in for a
// device without the R2's optional features (the R1). Lets the base-class defaults be
// tested without dragging another driver's dependencies into this binary.
class MinimalRefractometer : public RefractometerDevice {
    Q_OBJECT
public:
    bool isConnected() const override { return true; }
    double tds() const override { return 0.0; }
    double temperature() const override { return 0.0; }
    bool isMeasuring() const override { return false; }
    QString name() const override { return QStringLiteral("stub"); }
    void connectToDevice(const QBluetoothDeviceInfo&) override {}
    void disconnectFromDevice() override {}
    void requestMeasurement() override { ++m_singleRequests; }

    int m_singleRequests = 0;
};

// Records what the driver actually writes, so command tests assert transmitted bytes
// rather than re-deriving the expected ones.
class MockR2Transport : public ScaleBleTransport {
    Q_OBJECT
public:
    void connectToDevice(const QString&, const QString&) override {}
    void disconnectFromDevice() override {}
    void discoverServices() override {}
    void discoverCharacteristics(const QBluetoothUuid&) override {}
    void enableNotifications(const QBluetoothUuid&, const QBluetoothUuid&) override {}
    void writeCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&,
                             const QByteArray& value, WriteType = WriteType::WithResponse) override {
        m_writes.append(value);
    }
    void readCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&) override {}
    bool isConnected() const override { return true; }

    QList<QByteArray> m_writes;
};

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
    // Prism temp = temp * 10, tank temp = temp * 10, both in the DEVICE's unit.
    // unit: -1 omits Data5 entirely (firmware predating the unit byte, dataLen 5),
    //        0 = °C, 1 = °F.
    static QByteArray buildTemperaturePacket(double temp, int unit = -1) {
        uint16_t raw = static_cast<uint16_t>(qRound(temp * 10.0));
        QByteArray data;
        data.append(static_cast<char>(0x01));  // PackNo = 1 (temperature)
        data.append(static_cast<char>((raw >> 8) & 0xFF));  // Prism temp high
        data.append(static_cast<char>(raw & 0xFF));          // Prism temp low
        data.append(static_cast<char>((raw >> 8) & 0xFF));  // Tank temp high
        data.append(static_cast<char>(raw & 0xFF));          // Tank temp low
        if (unit >= 0)
            data.append(static_cast<char>(unit));            // Data5: 0 = °C, 1 = °F
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

    // Data5 = 0: the device is already reporting Celsius, pass through unchanged.
    void parseTemperaturePacketCelsiusUnit() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::temperatureChanged);

        r2.handlePacket(buildTemperaturePacket(23.50, /*unit=*/0));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 23.50);
        QCOMPARE(r2.temperature(), 23.50);
    }

    // Data5 = 1: the device is reporting Fahrenheit and the driver must convert.
    // Values are DiFluid's own worked example from protocolR2.md (79.1 °F prism).
    // Before Data5 was honoured this surfaced as "79.1 °C".
    void parseTemperaturePacketFahrenheitConverted() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::temperatureChanged);

        r2.handlePacket(buildTemperaturePacket(79.1, /*unit=*/1));

        QCOMPARE(spy.count(), 1);
        const double expectedC = (79.1 - 32.0) * 5.0 / 9.0;  // ≈ 26.17
        QVERIFY(qAbs(spy.at(0).at(0).toDouble() - expectedC) < 0.001);
        QVERIFY(qAbs(r2.temperature() - expectedC) < 0.001);
        // Guard the actual regression: never report the raw Fahrenheit number.
        QVERIFY(qAbs(r2.temperature() - 79.1) > 1.0);
    }

    // The R2 echoes a settings write back (Func 1, Cmd 0 = temperature unit).
    // That echo is log-only — it must not be mistaken for measurement data.
    void settingsEchoIsNotTreatedAsMeasurement() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tempSpy(&r2, &DiFluidR2::temperatureChanged);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);

        QByteArray data;
        data.append(static_cast<char>(0x00));  // 0 = °C
        r2.handlePacket(buildR2Packet(0x01, 0x00, data));

        QCOMPARE(tempSpy.count(), 0);
        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(r2.temperature(), 0.0);
        QCOMPARE(r2.tds(), 0.0);
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

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 error: No liquid .*class=2 code=3"));
        r2.handlePacket(buildErrorPacket(2, 3));  // errClass=2, errCode=3 = no liquid

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains("liquid"));
    }

    void parseErrorBeyondRange() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 error: Beyond range .*class=2 code=4"));
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

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("R2 error: benign device status .*class=0 code=2"));
        r2.handlePacket(buildErrorPacket(0, 2));

        QCOMPARE(errorSpy.count(), 0);
        // Not surfaced, but the measuring state is still cleared so the UI
        // doesn't hang on a benign status error.
        QCOMPARE(measSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    // === Auto Test ===
    //
    // The device starts a measurement itself when the sample is loaded. The setting
    // lives on the R2 and persists there, so Decenza reads it back rather than storing
    // a preference of its own.

    void autoTestDefaultsOffAndIsSupported() {
        DiFluidR2 r2(nullptr);
        QVERIFY(r2.supportsAutoTest());
        // Never assumed: until the device answers, we report the factory default.
        QVERIFY(!r2.autoTest());
    }

    void autoTestStateFollowsTheDeviceEcho() {
        DiFluidR2 r2(nullptr);
        QSignalSpy spy(&r2, &DiFluidR2::autoTestChanged);

        QByteArray on;
        on.append(static_cast<char>(0x01));
        r2.handlePacket(buildR2Packet(0x01, 0x01, on));
        QVERIFY(r2.autoTest());
        QCOMPARE(spy.count(), 1);

        QByteArray off;
        off.append(static_cast<char>(0x00));
        r2.handlePacket(buildR2Packet(0x01, 0x01, off));
        QVERIFY(!r2.autoTest());
        QCOMPARE(spy.count(), 2);

        // A redundant echo is not a change.
        r2.handlePacket(buildR2Packet(0x01, 0x01, off));
        QCOMPARE(spy.count(), 2);
    }

    void autoTestEchoIsNotTreatedAsMeasurement() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy tempSpy(&r2, &DiFluidR2::temperatureChanged);

        QByteArray on;
        on.append(static_cast<char>(0x01));
        r2.handlePacket(buildR2Packet(0x01, 0x01, on));

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(tempSpy.count(), 0);
    }

    void autoTestEchoDisagreeingWithTheRequestIsVisible() {
        // Settings holds the intent and is pushed on connect; the device's echo is the
        // only evidence it landed. If they diverge, the log has to say so — otherwise a
        // failed write looks identical to a successful one.
        DiFluidR2 r2(nullptr);
        QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);

        QByteArray off;
        off.append(static_cast<char>(0x00));
        r2.handlePacket(buildR2Packet(0x01, 0x01, off));

        bool stated = false;
        for (const auto& e : logSpy)
            if (e.at(0).toString().contains(QLatin1String("Auto Test is off"))) stated = true;
        QVERIFY2(stated, "the device's Auto Test state was not reported");
        QVERIFY(!r2.autoTest());
    }

    void deviceTestCountEchoDistinguishesAveragedFromSingle() {
        // This setting governs runs the DEVICE starts (physical button, Auto Test).
        // A log reader needs to be able to tell which mode a reading came from.
        DiFluidR2 r2(nullptr);
        QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);

        QByteArray three;
        three.append(static_cast<char>(0x03));
        r2.handlePacket(buildR2Packet(0x01, 0x03, three));

        QByteArray one;
        one.append(static_cast<char>(0x01));
        r2.handlePacket(buildR2Packet(0x01, 0x03, one));

        bool sawAveraged = false, sawSingle = false;
        for (const auto& e : logSpy) {
            const QString s = e.at(0).toString();
            if (s.contains(QLatin1String("count is now 3")) && s.contains(QLatin1String("averaged")))
                sawAveraged = true;
            if (s.contains(QLatin1String("count is now 1")) && s.contains(QLatin1String("single")))
                sawSingle = true;
        }
        QVERIFY2(sawAveraged, "a device test count above 1 was not reported as averaged");
        QVERIFY2(sawSingle, "a device test count of 1 was not reported as single");
    }

    void deviceTestCountEchoIsNotTreatedAsMeasurement() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy tempSpy(&r2, &DiFluidR2::temperatureChanged);

        QByteArray three;
        three.append(static_cast<char>(0x03));
        r2.handlePacket(buildR2Packet(0x01, 0x03, three));

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(tempSpy.count(), 0);
    }

    void deviceTestCountWriteRequiresAConnectedDevice() {
        DiFluidR2 r2(nullptr);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot set the device test count"));
        r2.setDeviceTestCount(3);
    }

    void autoTestWriteRequiresAConnectedDevice() {
        // The setting is written to the device, so there is nothing to do without one.
        // Silently no-oping would leave the user believing they had changed it.
        DiFluidR2 r2(nullptr);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot change Auto Test"));
        r2.setAutoTest(true);
        QVERIFY(!r2.autoTest());
    }

    void baseRefractometerDeclinesAutoTest() {
        // A device without the feature (the R1) must report that rather than pretend.
        // Exercised through the base defaults so this test does not drag the R1's
        // crypto dependencies into this binary.
        MinimalRefractometer plain;
        QVERIFY(!plain.supportsAutoTest());
        QVERIFY(!plain.autoTest());
        plain.setAutoTest(true);   // no-op, must not crash
        QVERIFY(!plain.autoTest());
    }

    void baseAveragedMeasurementFallsBackToASingleOne() {
        // Devices that cannot average still owe the caller a reading.
        MinimalRefractometer plain;
        plain.requestAveragedMeasurement(5);
        QCOMPARE(plain.m_singleRequests, 1);
    }

    // === Averaged measurement ===
    //
    // The R2 emits a full packet set per constituent test of an averaged run, so the
    // single-test result packet arrives once per test carrying THAT test's value. Which
    // packet is the real reading depends on the action code the response belongs to.
    // Packet shapes follow DiFluid's worked Average Test example in protocolR2.md.

    // Result packets under the Average Test action (Func 3, Cmd 1).
    static QByteArray buildAveragedRunPacket(uint8_t packNo, const QByteArray& payload) {
        QByteArray data;
        data.append(static_cast<char>(packNo));
        data.append(payload);
        return buildR2Packet(0x03, 0x01, data);
    }

    static QByteArray tdsPayload(double tds) {
        const quint16 raw = static_cast<quint16>(qRound(tds * 100.0));
        QByteArray p;
        p.append(static_cast<char>((raw >> 8) & 0xFF));
        p.append(static_cast<char>(raw & 0xFF));
        return p;
    }

    // Pack 4 under an averaged run: avg prism, avg tank, tests done, tests total.
    static QByteArray buildAverageProgressPacket(uint8_t completed, uint8_t total) {
        QByteArray p;
        p.append(QByteArray(4, '\0'));               // averaged prism + tank temps
        p.append(static_cast<char>(completed));
        p.append(static_cast<char>(total));
        return buildAveragedRunPacket(4, p);
    }

    static QByteArray buildAveragedRunStatusPacket(uint8_t status) {
        QByteArray p;
        p.append(static_cast<char>(status));
        return buildAveragedRunPacket(0, p);
    }

    // Put a driver in the state a connected one is in, so the bytes it writes can be
    // asserted. Previously these tests built the expected bytes and compared them to a
    // literal without ever calling the driver, which verified only their own arithmetic.
    static DiFluidR2* connectedDriver(MockR2Transport* transport) {
        auto* r2 = new DiFluidR2(transport);
        r2->m_connected = true;
        r2->m_characteristicsReady = true;
        return r2;
    }

    void averagedRequestSendsTheDocumentedBytes() {
        auto* transport = new MockR2Transport;
        QScopedPointer<DiFluidR2> r2(connectedDriver(transport));

        r2->requestAveragedMeasurement(3);

        // DF DF 03 01 01 03 C6 — verbatim from protocolR2.md's Average Test example.
        QVERIFY(!transport->m_writes.isEmpty());
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf03010103c6"));
    }

    void averagedTestCountIsClampedToTheDeviceRange() {
        auto* transport = new MockR2Transport;
        QScopedPointer<DiFluidR2> r2(connectedDriver(transport));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("outside the device range"));
        r2->requestAveragedMeasurement(25);
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf0301010acd"));  // 10

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("outside the device range"));
        r2->requestAveragedMeasurement(0);
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf03010101c4"));  // 1
    }

    void singleTestRequestSendsTheDocumentedBytes() {
        auto* transport = new MockR2Transport;
        QScopedPointer<DiFluidR2> r2(connectedDriver(transport));

        r2->requestMeasurement();

        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf030000c1"));
    }

    void deviceTestCountSendsTheDocumentedBytes() {
        auto* transport = new MockR2Transport;
        QScopedPointer<DiFluidR2> r2(connectedDriver(transport));

        r2->setDeviceTestCount(1);
        // DF DF 01 03 01 01 C4 — protocolR2.md's Set Number of Tests example.
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf01030101c4"));

        r2->setDeviceTestCount(99);
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf0103010acd"));  // clamped to 10
    }

    void autoTestSendsTheDocumentedBytes() {
        auto* transport = new MockR2Transport;
        QScopedPointer<DiFluidR2> r2(connectedDriver(transport));

        r2->setAutoTest(true);
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf01010101c2"));
        r2->setAutoTest(false);
        QCOMPARE(transport->m_writes.last().toHex(), QByteArray("dfdf01010100c1"));
    }

    void aNonProgressStatusDoesNotKeepTheRunAlive() {
        // r2StatusIsProgress could `return true` unconditionally and every other test
        // still passes — which would defeat the watchdog entirely, the one thing the
        // no-timers-as-guards exception was granted to preserve.
        DiFluidR2 r2(nullptr);
        beginMeasuringWithShortWatchdog(r2, 60);

        QTest::qWait(40);
        r2.handlePacket(buildStatusPacket(1));  // Calibration finished — not progress

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Measurement timeout"));
        QTest::qWait(200);
        QVERIFY2(!r2.isMeasuring(), "a non-progress status kept the watchdog alive");
    }

    void perTestResultDuringAveragedRunIsNotAReading() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        beginMeasuringWithShortWatchdog(r2, 500);

        // Pack 2 under Cmd 1: one test of several, not the answer.
        r2.handlePacket(buildAveragedRunPacket(2, tdsPayload(9.99)));

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(completeSpy.count(), 0);
        QVERIFY2(r2.isMeasuring(), "an individual test ended the averaged run");
    }

    void singleTestResultIsStillTerminal() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        beginMeasuringWithShortWatchdog(r2, 500);

        r2.handlePacket(buildTdsPacket(8.50));  // Func 3, Cmd 0, pack 2

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(completeSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    void unrecognisedActionCodeStillDeliversAReading() {
        // We do not know what action code a physical-button measurement carries. An
        // unrecognised one must behave as it did before the dispatch existed — a reading —
        // never silence.
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);

        QByteArray data;
        data.append(static_cast<char>(0x02));  // pack 2
        data.append(tdsPayload(8.50));
        r2.handlePacket(buildR2Packet(0x03, 0x07, data));  // Cmd 7: unknown action

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(tdsSpy.at(0).at(0).toDouble(), 8.50);
    }

    void averagedRunEmitsEachConvergingAverageAndCompletesOnlyAtTheEnd() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        QSignalSpy progressSpy(&r2, &DiFluidR2::averageProgress);
        beginMeasuringWithShortWatchdog(r2, 500);

        const double runningAverages[] = { 8.40, 8.46, 8.50 };
        for (int test = 0; test < 3; ++test) {
            r2.handlePacket(buildAveragedRunStatusPacket(5));                     // ongoing
            r2.handlePacket(buildAveragedRunPacket(2, tdsPayload(8.60)));         // this test
            r2.handlePacket(buildAveragedRunPacket(3, tdsPayload(runningAverages[test])));
            r2.handlePacket(buildAverageProgressPacket(test + 1, 3));

            // Each averaged result is delivered, but the run is not over.
            QCOMPARE(tdsSpy.count(), test + 1);
            QCOMPARE(completeSpy.count(), 0);
            QVERIFY(r2.isMeasuring());
        }

        QCOMPARE(progressSpy.count(), 3);
        QCOMPARE(progressSpy.at(2).at(0).toInt(), 3);
        QCOMPARE(progressSpy.at(2).at(1).toInt(), 3);

        // Terminal status ends the run; the value is already in hand.
        r2.handlePacket(buildAveragedRunStatusPacket(6));
        QCOMPARE(completeSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
        QCOMPARE(r2.tds(), 8.50);
    }

    void missingTerminalStatusDoesNotLoseTheReading() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        beginMeasuringWithShortWatchdog(r2, 50);

        r2.handlePacket(buildAveragedRunPacket(3, tdsPayload(8.50)));

        // Status 6 never arrives — the watchdog clears the spinner, but the user keeps
        // the reading. Losing the value here is the failure mode this design avoids.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Measurement timeout"));
        QTest::qWait(200);

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(r2.tds(), 8.50);
        QVERIFY(!r2.isMeasuring());
    }

    void deviceInitiatedAveragedRunStillDelivers() {
        // The R2's own test-count setting applies to measurements started on the device,
        // so a run the app never requested can be a multi-test average. m_measuring is
        // false throughout — handling must not depend on having asked.
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QVERIFY(!r2.isMeasuring());

        r2.handlePacket(buildAveragedRunStatusPacket(4));
        r2.handlePacket(buildAveragedRunPacket(2, tdsPayload(8.60)));
        r2.handlePacket(buildAveragedRunPacket(3, tdsPayload(8.50)));
        r2.handlePacket(buildAverageProgressPacket(1, 1));
        r2.handlePacket(buildAveragedRunStatusPacket(6));

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(r2.tds(), 8.50);
        // No watchdog was ever started on its account.
        QVERIFY(!r2.m_measurementTimer.isActive());
    }

    void averagedResultIsGatedLikeAnyOtherReading() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
        beginMeasuringWithShortWatchdog(r2, 500);

        // The out-of-range sentinel lands in the same field as a real reading.
        QByteArray payload;
        payload.append(static_cast<char>(0xFF));
        payload.append(static_cast<char>(0xE5));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("out of range"));
        r2.handlePacket(buildAveragedRunPacket(3, payload));

        // Rejected, but mid-run the run continues: ending our side while the R2 keeps
        // measuring would show an error dialog and then a good reading seconds later,
        // with the button re-enabled so a second run could be stacked on the first.
        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(errorSpy.count(), 0);
        QVERIFY2(r2.isMeasuring(), "a bad mid-run reading ended the run");
    }

    void terminalOutOfRangeReadingEndsTheRunWithAnError() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
        beginMeasuringWithShortWatchdog(r2, 500);

        QByteArray payload;
        payload.append(static_cast<char>(0xFF));
        payload.append(static_cast<char>(0xE5));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("out of range"));
        r2.handlePacket(buildR2Packet(0x03, 0x00, QByteArray(1, char(2)) + payload));

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    // === Loop test (Cmd 3) ===
    //
    // Undocumented by DiFluid and absent from Beanconqueror's enum; observed on hardware
    // as what Auto Test escalates to on an unsettled prism. It re-measures until the
    // reading stops moving, then ends with status 9. Byte sequence below is a real run:
    // 7.65 then 7.64 four times, converging and stopping.

    static QByteArray buildLoopRunPacket(uint8_t packNo, const QByteArray& payload) {
        QByteArray data;
        data.append(static_cast<char>(packNo));
        data.append(payload);
        return buildR2Packet(0x03, 0x03, data);
    }

    void loopRunDeliversEachReadingWithoutEndingTheRun() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        beginMeasuringWithShortWatchdog(r2, 500);

        r2.handlePacket(buildLoopRunPacket(2, tdsPayload(7.65)));
        r2.handlePacket(buildLoopRunPacket(0, QByteArray(1, char(8))));   // ongoing
        r2.handlePacket(buildLoopRunPacket(2, tdsPayload(7.64)));
        r2.handlePacket(buildLoopRunPacket(2, tdsPayload(7.64)));

        // Every reading reaches consumers — latest wins, so the settled value survives.
        QCOMPARE(tdsSpy.count(), 3);
        QCOMPARE(r2.tds(), 7.64);
        // But the run is one settling measurement, not three finished ones.
        QCOMPARE(completeSpy.count(), 0);
        QVERIFY(r2.isMeasuring());

        r2.handlePacket(buildLoopRunPacket(0, QByteArray(1, char(9))));  // loop finished
        QCOMPARE(completeSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
        QCOMPARE(r2.tds(), 7.64);
    }

    void loopAndAveragedProgressNotesDescribeTheRightMechanism() {
        // A loop test re-measures one sample until it settles; an averaged run converges
        // on a mean. Calling a loop reading a "running average" names the wrong thing.
        DiFluidR2 loop(nullptr);
        QSignalSpy loopLog(&loop, &DiFluidR2::logMessage);
        beginMeasuringWithShortWatchdog(loop, 500);
        loop.handlePacket(buildLoopRunPacket(2, tdsPayload(7.65)));

        bool loopSettling = false, loopMislabelled = false;
        for (const auto& e : loopLog) {
            const QString s = e.at(0).toString();
            if (s.contains(QLatin1String("still settling"))) loopSettling = true;
            if (s.contains(QLatin1String("running average"))) loopMislabelled = true;
        }
        QVERIFY2(loopSettling, "loop reading was not described as still settling");
        QVERIFY2(!loopMislabelled, "loop reading was described as a running average");

        DiFluidR2 avg(nullptr);
        QSignalSpy avgLog(&avg, &DiFluidR2::logMessage);
        beginMeasuringWithShortWatchdog(avg, 500);
        avg.handlePacket(buildAveragedRunPacket(3, tdsPayload(7.83)));

        bool avgLabelled = false;
        for (const auto& e : avgLog)
            if (e.at(0).toString().contains(QLatin1String("running average"))) avgLabelled = true;
        QVERIFY2(avgLabelled, "averaged reading was not described as a running average");
    }

    void singleTestIsStillTerminalOnItsOwnResult() {
        // The loop-run handling must not change Cmd 0, which is what both the app button
        // and a physical R2 button press use.
        DiFluidR2 r2(nullptr);
        QSignalSpy completeSpy(&r2, &DiFluidR2::measurementComplete);
        beginMeasuringWithShortWatchdog(r2, 500);

        r2.handlePacket(buildTdsPacket(7.64));

        QCOMPARE(completeSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
    }

    // === Measurement liveness watchdog ===
    //
    // The watchdog exists to recover from a device that goes silent, which produces no
    // event for an event-based mechanism to see. It must NOT double as a ceiling on how
    // long a legitimate measurement may take — armed once at request time it was exactly
    // that, and an averaged run of several tests would trip it mid-run.
    //
    // Tests shorten the interval; 15s is not a unit-test timescale.

    // Put the driver in the state requestMeasurement() leaves it in, with a short
    // watchdog so a test can outlive it in a few hundred milliseconds.
    static void beginMeasuringWithShortWatchdog(DiFluidR2& r2, int intervalMs) {
        r2.m_measuring = true;
        r2.m_measurementTimer.setInterval(intervalMs);
        r2.m_measurementTimer.start();
    }

    void progressPacketsKeepALongRunAlive() {
        DiFluidR2 r2(nullptr);
        beginMeasuringWithShortWatchdog(r2, 100);

        // Five progress packets at 60ms — 300ms total, three watchdog intervals.
        // Any timeout would emit a warning and fail the test via failOnWarning().
        for (int i = 0; i < 5; ++i) {
            QTest::qWait(60);
            r2.handlePacket(buildStatusPacket(5));  // Average test ongoing
        }

        QVERIFY2(r2.isMeasuring(), "healthy long run was aborted by the watchdog");
        QVERIFY(r2.m_measurementTimer.isActive());
    }

    void slowTestStatusRestartsTheWatchdog() {
        DiFluidR2 r2(nullptr);
        beginMeasuringWithShortWatchdog(r2, 100);

        // Status 10 is the R2 saying "this individual test is running long" — precisely
        // when a fixed deadline would have fired.
        QTest::qWait(60);
        r2.handlePacket(buildStatusPacket(10));
        QTest::qWait(60);

        QVERIFY(r2.isMeasuring());
        QVERIFY(r2.m_measurementTimer.isActive());
    }

    void temperaturePacketIsProgressToo() {
        DiFluidR2 r2(nullptr);
        beginMeasuringWithShortWatchdog(r2, 100);

        QTest::qWait(60);
        r2.handlePacket(buildTemperaturePacket(23.5, /*unit=*/0));
        QTest::qWait(60);

        QVERIFY(r2.isMeasuring());
    }

    void silenceStillTimesOut() {
        // The property the no-timers-as-guards exception was granted for: a device that
        // stops answering entirely must not leave the UI waiting forever.
        DiFluidR2 r2(nullptr);
        QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);
        beginMeasuringWithShortWatchdog(r2, 50);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Measurement timeout"));
        QTest::qWait(200);

        QVERIFY2(!r2.isMeasuring(), "silent device did not time out");
        QCOMPARE(measSpy.count(), 1);
    }

    void resultStopsTheWatchdogAndLaterStatusChangesNothing() {
        DiFluidR2 r2(nullptr);
        QSignalSpy tdsSpy(&r2, &DiFluidR2::tdsChanged);
        beginMeasuringWithShortWatchdog(r2, 100);

        r2.handlePacket(buildAverageTdsPacket(1.35));
        QCOMPARE(tdsSpy.count(), 1);
        QVERIFY(!r2.isMeasuring());
        QVERIFY(!r2.m_measurementTimer.isActive());

        // Status 6 (Average Test Finished) trails the result; it must not resurrect
        // the run or re-fire anything.
        r2.handlePacket(buildStatusPacket(6));
        QVERIFY(!r2.isMeasuring());
        QVERIFY(!r2.m_measurementTimer.isActive());
        QCOMPARE(tdsSpy.count(), 1);
    }

    void disconnectMidRunClearsMeasuring() {
        DiFluidR2 r2(nullptr);
        QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);
        beginMeasuringWithShortWatchdog(r2, 100);

        // An averaged run holds the measuring state open far longer than a single test,
        // so losing the link mid-run is a realistic case rather than a 2-second window.
        r2.onTransportDisconnected();

        QVERIFY(!r2.isMeasuring());
        QVERIFY(!r2.m_measurementTimer.isActive());
        QVERIFY(measSpy.count() >= 1);
    }

    // === Serial number reassembly ===
    //
    // Byte vectors are DiFluid's own worked example from protocolR2.md, which
    // decodes to "68B6B32417B0000".

    static QByteArray buildSerialPacket(uint8_t part, const char* fiveChars) {
        QByteArray data;
        data.append(static_cast<char>(part));
        data.append(QByteArray(fiveChars, 5));
        return buildR2Packet(0x00, 0x00, data);
    }

    void serialNumberAssembledFromThreeParts() {
        DiFluidR2 r2(nullptr);
        QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);

        r2.handlePacket(buildSerialPacket(0, "68B6B"));
        r2.handlePacket(buildSerialPacket(1, "32417"));
        r2.handlePacket(buildSerialPacket(2, "B0000"));

        QCOMPARE(r2.m_serialNumber, QStringLiteral("68B6B32417B0000"));
        bool logged = false;
        for (const auto& e : logSpy)
            if (e.at(0).toString().contains(QLatin1String("68B6B32417B0000"))) logged = true;
        QVERIFY2(logged, "complete serial number was not logged");
    }

    void serialNumberPartsArrivingOutOfOrder() {
        DiFluidR2 r2(nullptr);

        // Deliberately reversed — BLE notification ordering is not guaranteed.
        r2.handlePacket(buildSerialPacket(2, "B0000"));
        r2.handlePacket(buildSerialPacket(0, "68B6B"));
        r2.handlePacket(buildSerialPacket(1, "32417"));

        QCOMPARE(r2.m_serialNumber, QStringLiteral("68B6B32417B0000"));
    }

    void partialSerialNumberIsNotReportedAsIdentity() {
        DiFluidR2 r2(nullptr);
        QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);

        r2.handlePacket(buildSerialPacket(0, "68B6B"));
        r2.handlePacket(buildSerialPacket(2, "B0000"));

        // Middle part missing: no serial number exists yet.
        QVERIFY(r2.m_serialNumber.isEmpty());
        for (const auto& e : logSpy) {
            const QString line = e.at(0).toString();
            QVERIFY2(!line.contains(QLatin1String("Serial number: ")),
                     "a partial serial was logged as the device identity");
        }
    }

    void malformedSerialPartIsRejected() {
        DiFluidR2 r2(nullptr);

        // Part index beyond the three the device sends.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("part index 9 out of range"));
        r2.handlePacket(buildSerialPacket(9, "XXXXX"));

        // Truncated payload.
        QByteArray shortData;
        shortData.append(static_cast<char>(0x00));
        shortData.append("AB", 2);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Serial number packet too short"));
        r2.handlePacket(buildR2Packet(0x00, 0x00, shortData));

        QVERIFY(r2.m_serialNumber.isEmpty());
    }

    // === Status and error naming ===
    //
    // Field logs are read by people and by AI assistants triaging over MCP, so the
    // names are an interface. These tests assert on the emitted log text.

    void statusCodesAreLoggedByName() {
        struct { uint8_t code; const char* fragment; } cases[] = {
            {  0, "Test finished" },
            {  1, "Calibration finished" },
            {  4, "Average test started" },
            {  5, "Average test ongoing" },
            {  6, "Average test finished" },
            {  7, "Loop test started" },
            {  8, "Loop test ongoing" },
            {  9, "Loop test finished" },
            { 10, "running long" },
            { 11, "Test started" },
            { 12, "Calibration started" },
        };

        for (const auto& c : cases) {
            DiFluidR2 r2(nullptr);
            QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);
            r2.handlePacket(buildStatusPacket(c.code));

            bool found = false;
            for (const auto& entry : logSpy)
                if (entry.at(0).toString().contains(QLatin1String(c.fragment))) found = true;
            QVERIFY2(found, qPrintable(QString("status %1 was not logged as \"%2\"")
                                           .arg(c.code).arg(c.fragment)));
        }
    }

    void unknownStatusCodeStillLogsItsNumber() {
        DiFluidR2 r2(nullptr);
        QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);
        QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);

        r2.handlePacket(buildStatusPacket(200));

        bool found = false;
        for (const auto& entry : logSpy)
            if (entry.at(0).toString().contains(QLatin1String("200"))) found = true;
        QVERIFY2(found, "unnamed status code was not logged with its number");
        // An unrecognised status must not move the measurement state machine.
        QCOMPARE(measSpy.count(), 0);
    }

    void hardwareErrorNamesTheOnScreenCode() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
        QSignalSpy logSpy(&r2, &DiFluidR2::logMessage);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("hardware error.*code 7"));
        r2.handlePacket(buildErrorPacket(3, 7));

        bool found = false;
        for (const auto& entry : logSpy)
            if (entry.at(0).toString().contains(QLatin1String("code 7"))) found = true;
        QVERIFY2(found, "hardware error did not report the code shown on the device screen");
        // Naming a code is not the same as surfacing it — the user-actionable
        // division is unchanged.
        QCOMPARE(errorSpy.count(), 0);
    }

    void newlyNamedErrorCodesDoNotBecomeNewDialogs() {
        // Class 2 codes 1 and 2 are named as of this change, but the division over
        // what reaches the user is deliberately untouched: only no-liquid (3) and
        // beyond-range (4) are things a user can act on.
        for (uint8_t code : { uint8_t(1), uint8_t(2) }) {
            DiFluidR2 r2(nullptr);
            QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
            QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);

            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("R2 error: .*class=2"));
            r2.handlePacket(buildErrorPacket(2, code));

            QCOMPARE(errorSpy.count(), 0);
            // Measuring state still clears so the UI cannot hang on any error.
            QCOMPARE(measSpy.count(), 1);
            QVERIFY(!r2.isMeasuring());
        }
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
