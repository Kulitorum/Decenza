#include <QtTest>
#include <QSignalSpy>

#include "ble/refractometers/difluidr2.h"
#include "ble/protocol/de1characteristics.h"

// Test DiFluid R2 refractometer BLE packet parsing, checksum validation,
// and device name matching.
//
// Protocol: header 0xDF 0xDF, func, cmd, datalen, data, additive checksum.
// Func 3 = Device Action. Pack 0 = status, Pack 1 = temperature, Pack 2 = TDS.

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

        r2.handlePacket(buildErrorPacket(2, 3));  // errClass=2, errCode=3 = no liquid

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains("liquid"));
    }

    void parseErrorBeyondRange() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);

        r2.handlePacket(buildErrorPacket(2, 4));  // errClass=2, errCode=4 = beyond range

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains("range"));
    }

    void parseErrorUnknown() {
        DiFluidR2 r2(nullptr);
        QSignalSpy errorSpy(&r2, &DiFluidR2::errorOccurred);
        QSignalSpy measSpy(&r2, &DiFluidR2::measuringChanged);

        // Cmd=255 = unknown error
        r2.handlePacket(buildR2Packet(0x03, 0xFF, QByteArray()));

        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(errorSpy.at(0).at(0).toString().contains("Unknown"));
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
