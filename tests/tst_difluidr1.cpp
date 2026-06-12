#include <QSignalSpy>
#include <QtTest>
#include <QRegularExpression>

#include "ble/protocol/de1characteristics.h"
#include "ble/refractometers/aes128.h"
#include "ble/refractometers/difluidr1.h"

// Test DiFluid R1 refractometer against the byte-exact vectors captured from
// a physical device running the official DiFluid app v1.2.6:
//
//   Salt:          B8 D6 1B B5 DC 1A 03 8A 06 5E 15 09
//   Derived key:   B8 D6 1A B4 DA 18 AA AA AA AA AA AA AA AA AA AA
//   Sample ct:     5A 06 A0 05 22 D2 33 4D 0F 0B 28 F6 78 D9 DE CA
//   Decrypted pt:  00 00 09 B7 FF FF FF B9 00 02 08 46 FF FF FF B9
//   Parsed:        T=24.87°C  Brix=-0.71%  RI=1.33190  raw=-0.71
//
//   doWrite(-0.57, "TDS"):
//     plaintext:   FF FF FF C7 03 54 44 53 00 00 00 00 00 00 00 00
//     ciphertext:  16 46 56 D7 95 B8 83 3E 06 C0 B0 3E B9 22 BA 75
//     frame:       06 16 46 56 D7 95 B8 83 3E 06 C0 B0 3E B9 22 BA 75
//
// Driver-level tests follow the R2 pattern: instantiate with a null transport
// and drive the private handlers via friend access.

namespace {

QByteArray hexToBytes(const char* hex) {
    return QByteArray::fromHex(QByteArray(hex).replace(' ', ""));
}

// Sample salt + derived key reused by several driver tests below.
const QByteArray kSalt = hexToBytes("B8 D6 1B B5 DC 1A 03 8A 06 5E 15 09");

// Encrypt an arbitrary plaintext under the test key — used to build
// driver-input ciphertexts whose decoded values we can predict.
QByteArray encryptUnderTestKey(const QByteArray& plaintext,
                               const std::array<uint8_t, 16>& key) {
    Q_ASSERT(plaintext.size() == 16);
    // Round-trip via decrypt(encrypt(...)) would be easier but we want the
    // ciphertext on its own; reuse buildDoWriteFrame's encryption helper.
    // Easier: encrypt by feeding plaintext through encryptBlock directly.
    QByteArray ct(16, Qt::Uninitialized);
    decenza::aes128::encryptBlock(
        key,
        reinterpret_cast<const uint8_t*>(plaintext.constData()),
        reinterpret_cast<uint8_t*>(ct.data()));
    return ct;
}

// Build a 16-byte plaintext encoding (temperature, brix, RI, sample).
QByteArray makePlaintext(double tempC, double brix, double ri, double sample) {
    QByteArray pt(16, '\0');
    auto* p = reinterpret_cast<uchar*>(pt.data());
    qToBigEndian<qint32>(static_cast<qint32>(qRound(tempC * 100.0)), p + 0);
    qToBigEndian<qint32>(static_cast<qint32>(qRound(brix  * 100.0)), p + 4);
    qToBigEndian<quint32>(static_cast<quint32>(qRound(ri  * 100000.0)), p + 8);
    qToBigEndian<qint32>(static_cast<qint32>(qRound(sample * 100.0)), p + 12);
    return pt;
}

} // namespace

class tst_DiFluidR1 : public QObject {
    Q_OBJECT

private slots:

    // === Pure helpers — captured vectors ===

    void deriveKey_matchesIssueVector() {
        const auto key = DiFluidR1::deriveKey(kSalt);
        QByteArray actual(reinterpret_cast<const char*>(key.data()), 16);
        const QByteArray expected = hexToBytes("B8 D6 1A B4 DA 18 AA AA AA AA AA AA AA AA AA AA");
        QCOMPARE(actual, expected);
    }

    void decryptFrame_matchesIssueVector() {
        const auto key = DiFluidR1::deriveKey(kSalt);
        const QByteArray ct = hexToBytes("5A 06 A0 05 22 D2 33 4D 0F 0B 28 F6 78 D9 DE CA");
        const QByteArray pt = DiFluidR1::decryptFrame(ct, key);
        const QByteArray expected = hexToBytes("00 00 09 B7 FF FF FF B9 00 02 08 46 FF FF FF B9");
        QCOMPARE(pt, expected);
    }

    void parsePlaintext_matchesIssueVector() {
        const QByteArray pt = hexToBytes("00 00 09 B7 FF FF FF B9 00 02 08 46 FF FF FF B9");
        double tempC = 0, brix = 0, ri = 0, raw = 0;
        QVERIFY(DiFluidR1::parsePlaintext(pt, tempC, brix, ri, raw));
        QCOMPARE(tempC, 24.87);
        QCOMPARE(brix, -0.71);
        QCOMPARE(ri, 1.33190);
        QCOMPARE(raw, -0.71);
    }

    void buildDoWrite_matchesIssueVector() {
        const auto key = DiFluidR1::deriveKey(kSalt);
        const QByteArray frame = DiFluidR1::buildDoWriteFrame(-0.57, QByteArrayLiteral("TDS"), key);
        const QByteArray expected =
            hexToBytes("06 16 46 56 D7 95 B8 83 3E 06 C0 B0 3E B9 22 BA 75");
        QCOMPARE(frame, expected);
    }

    void parsePlaintext_rejectsWrongSize() {
        QByteArray pt(15, '\0');
        double t = 0, b = 0, r = 0, s = 0;
        QVERIFY(!DiFluidR1::parsePlaintext(pt, t, b, r, s));
    }

    void decryptFrame_rejectsWrongSize() {
        std::array<uint8_t, 16> key{};
        QByteArray ct(15, '\0');
        QCOMPARE(DiFluidR1::decryptFrame(ct, key).size(), 0);
    }

    void deriveKey_padsShortSaltWithZero() {
        // Salt with only 4 bytes; remaining key slots derived from "0 - i/2".
        const QByteArray salt = hexToBytes("10 20 30 40");
        const auto key = DiFluidR1::deriveKey(salt);
        // key[0] = 0x10 - 0    = 0x10
        // key[1] = 0x20 - 0    = 0x20
        // key[2] = 0x30 - 1    = 0x2F
        // key[3] = 0x40 - 1    = 0x3F
        // key[4] = 0    - 2    = 0xFE
        // key[5] = 0    - 2    = 0xFE
        QCOMPARE(key[0], uint8_t(0x10));
        QCOMPARE(key[1], uint8_t(0x20));
        QCOMPARE(key[2], uint8_t(0x2F));
        QCOMPARE(key[3], uint8_t(0x3F));
        QCOMPARE(key[4], uint8_t(0xFE));
        QCOMPARE(key[5], uint8_t(0xFE));
        for (int i = 6; i < 16; ++i) QCOMPARE(key[i], uint8_t(0xAA));
    }

    void isR1Device_recognizesPrefix() {
        QVERIFY(DiFluidR1::isR1Device("DFT_TDJ_301033"));
        QVERIFY(DiFluidR1::isR1Device("dft_tdj_001"));  // case-insensitive
        QVERIFY(!DiFluidR1::isR1Device("R2 Extract"));
        QVERIFY(!DiFluidR1::isR1Device("DiFluid R2"));
        QVERIFY(!DiFluidR1::isR1Device("difluid"));      // microbalance scale
        QVERIFY(!DiFluidR1::isR1Device(""));
    }

    // === Driver behavior — gates and state machine ===

    void handleMeasurementFrame_dropsWhenKeyNotReady() {
        DiFluidR1 r1(nullptr);
        QSignalSpy tdsSpy(&r1, &RefractometerDevice::tdsChanged);
        QSignalSpy tempSpy(&r1, &RefractometerDevice::temperatureChanged);
        QSignalSpy completeSpy(&r1, &RefractometerDevice::measurementComplete);

        // m_phase starts at Disconnected. Any valid-sized ct must be dropped.
        // The drop is logged as a warning — that's the behaviour under test.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Measurement frame arrived before key derivation"));
        const QByteArray ct = hexToBytes("5A 06 A0 05 22 D2 33 4D 0F 0B 28 F6 78 D9 DE CA");
        r1.handleMeasurementFrame(ct);

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(tempSpy.count(), 0);
        QCOMPARE(completeSpy.count(), 0);
        QCOMPARE(r1.tds(), 0.0);
        QCOMPARE(r1.temperature(), 0.0);
    }

    void handleMeasurementFrame_dropsNonProtocolSizes() {
        DiFluidR1 r1(nullptr);
        r1.m_key = DiFluidR1::deriveKey(kSalt);
        r1.m_phase = DiFluidR1::Phase::Ready;
        QSignalSpy tdsSpy(&r1, &RefractometerDevice::tdsChanged);

        r1.handleMeasurementFrame(QByteArray());
        r1.handleMeasurementFrame(QByteArray(1, '\0'));
        r1.handleMeasurementFrame(QByteArray(15, '\0'));
        r1.handleMeasurementFrame(QByteArray(17, '\0'));
        QCOMPARE(tdsSpy.count(), 0);
    }

    void handleMeasurementFrame_emitsRiBandWarning_outOfBand() {
        DiFluidR1 r1(nullptr);
        const auto key = DiFluidR1::deriveKey(kSalt);
        r1.m_key = key;
        r1.m_phase = DiFluidR1::Phase::Ready;
        QSignalSpy logSpy(&r1, &RefractometerDevice::logMessage);

        // RI = 1.20 — below the [1.30, 1.50] band; the out-of-band warning
        // is the behaviour under test.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("outside the plausible band"));
        const QByteArray pt = makePlaintext(25.0, 2.0, 1.20, 2.0);
        r1.handleMeasurementFrame(encryptUnderTestKey(pt, key));

        bool sawWarning = false;
        for (const QList<QVariant>& args : logSpy) {
            if (args.first().toString().contains("outside the plausible band")) {
                sawWarning = true;
                break;
            }
        }
        QVERIFY(sawWarning);
    }

    void handleMeasurementFrame_noRiWarning_inBand() {
        DiFluidR1 r1(nullptr);
        const auto key = DiFluidR1::deriveKey(kSalt);
        r1.m_key = key;
        r1.m_phase = DiFluidR1::Phase::Ready;
        QSignalSpy logSpy(&r1, &RefractometerDevice::logMessage);

        // RI = 1.40 — in band.
        const QByteArray pt = makePlaintext(25.0, 2.0, 1.40, 2.0);
        r1.handleMeasurementFrame(encryptUnderTestKey(pt, key));

        for (const QList<QVariant>& args : logSpy) {
            QVERIFY2(!args.first().toString().contains("outside the plausible band"),
                     "Should not warn about RI when in band");
        }
    }

    void requestMeasurement_isNoOpWhenNotReady() {
        DiFluidR1 r1(nullptr);
        QSignalSpy measuringSpy(&r1, &RefractometerDevice::measuringChanged);

        // Disconnected — must not flip measuring. Each no-op read logs a
        // warning (one per requestMeasurement call below).
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot read — not connected"));
        r1.requestMeasurement();
        QCOMPARE(measuringSpy.count(), 0);
        QVERIFY(!r1.isMeasuring());
        QVERIFY(!r1.m_measurementTimer.isActive());

        // CharacteristicsReady (chars discovered, key not derived yet).
        r1.m_phase = DiFluidR1::Phase::CharacteristicsReady;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Cannot read — not connected"));
        r1.requestMeasurement();
        QCOMPARE(measuringSpy.count(), 0);
        QVERIFY(!r1.isMeasuring());
        QVERIFY(!r1.m_measurementTimer.isActive());
    }

    void measurementTimer_clearsMeasuringFlag() {
        DiFluidR1 r1(nullptr);
        r1.m_measuring = true;
        QSignalSpy spy(&r1, &RefractometerDevice::measuringChanged);

        // Directly fire the singleshot timeout slot via QMetaObject — the
        // timer is already wired in the constructor. setInterval(0) + start
        // ensures the lambda runs on the next event loop tick.
        // The timeout slot logs a warning — expected.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Measurement timeout"));
        r1.m_measurementTimer.setInterval(0);
        r1.m_measurementTimer.start();
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(!r1.isMeasuring());
    }

    void disconnectFromDevice_resetsAllState() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::Ready;
        r1.m_measuring = true;
        r1.m_measurementTimer.start(60000);
        r1.m_initTimer.start(60000);
        r1.m_saltWatchdog.start(60000);
        QSignalSpy connSpy(&r1, &RefractometerDevice::connectedChanged);
        QSignalSpy measSpy(&r1, &RefractometerDevice::measuringChanged);

        r1.disconnectFromDevice();

        QCOMPARE(r1.m_phase, DiFluidR1::Phase::Disconnected);
        QVERIFY(!r1.isMeasuring());
        QVERIFY(!r1.m_measurementTimer.isActive());
        QVERIFY(!r1.m_initTimer.isActive());
        QVERIFY(!r1.m_saltWatchdog.isActive());
        QCOMPARE(connSpy.count(), 1);
        QCOMPARE(measSpy.count(), 1);
    }

    void onTransportDisconnected_resetsAllState() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::Ready;
        r1.m_measuring = true;
        r1.m_measurementTimer.start(60000);
        r1.m_initTimer.start(60000);
        r1.m_saltWatchdog.start(60000);
        QSignalSpy connSpy(&r1, &RefractometerDevice::connectedChanged);

        r1.onTransportDisconnected();

        QCOMPARE(r1.m_phase, DiFluidR1::Phase::Disconnected);
        QVERIFY(!r1.isMeasuring());
        QVERIFY(!r1.m_measurementTimer.isActive());
        QVERIFY(!r1.m_initTimer.isActive());
        QVERIFY(!r1.m_saltWatchdog.isActive());
        QCOMPARE(connSpy.count(), 1);
    }

    void onTransportError_resetsAllState() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::Ready;
        r1.m_measurementTimer.start(60000);
        r1.m_initTimer.start(60000);
        r1.m_saltWatchdog.start(60000);

        // onTransportError logs the reason — expected.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Transport error: simulated error"));
        r1.onTransportError("simulated error");

        QCOMPARE(r1.m_phase, DiFluidR1::Phase::Disconnected);
        QVERIFY(!r1.m_measurementTimer.isActive());
        QVERIFY(!r1.m_initTimer.isActive());
        QVERIFY(!r1.m_saltWatchdog.isActive());
    }

    void emitTdsResult_clampsAboveMax_andEmitsError() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::Ready;
        r1.m_measuring = true;
        r1.m_measurementTimer.start(60000);
        QSignalSpy tdsSpy(&r1, &RefractometerDevice::tdsChanged);
        QSignalSpy errSpy(&r1, &RefractometerDevice::errorOccurred);
        QSignalSpy measSpy(&r1, &RefractometerDevice::measuringChanged);

        // Out-of-range Brix is rejected with a warning — that's the test.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Brix out of range"));
        r1.emitTdsResult(40.0, 25.0);  // above 35.0 ceiling

        QCOMPARE(tdsSpy.count(), 0);
        QCOMPARE(errSpy.count(), 1);
        QCOMPARE(r1.tds(), 0.0);
        QVERIFY(!r1.isMeasuring());
        QVERIFY(!r1.m_measurementTimer.isActive());
        QCOMPARE(measSpy.count(), 1);
    }

    void emitTdsResult_clampsBelowNegativeMax_andEmitsError() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::Ready;
        QSignalSpy errSpy(&r1, &RefractometerDevice::errorOccurred);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Brix out of range"));
        r1.emitTdsResult(-40.0, 25.0);  // R1 gates symmetrically (R2 does not)

        QCOMPARE(errSpy.count(), 1);
    }

    void emitTdsResult_emitsCompleteOnValidValue() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::Ready;
        r1.m_measuring = true;
        r1.m_measurementTimer.start(60000);
        QSignalSpy tdsSpy(&r1, &RefractometerDevice::tdsChanged);
        QSignalSpy completeSpy(&r1, &RefractometerDevice::measurementComplete);

        r1.emitTdsResult(2.50, 25.0);

        QCOMPARE(tdsSpy.count(), 1);
        QCOMPARE(completeSpy.count(), 1);
        QCOMPARE(r1.tds(), 2.50);
        QVERIFY(!r1.isMeasuring());
        QVERIFY(!r1.m_measurementTimer.isActive());
    }

    void onCharacteristicRead_shortSaltDoesNotConnect() {
        DiFluidR1 r1(nullptr);  // null transport — disconnectFromDevice is safe
        r1.m_phase = DiFluidR1::Phase::CharacteristicsReady;
        QSignalSpy connSpy(&r1, &RefractometerDevice::connectedChanged);

        // 5 bytes — below the 6-byte minimum; the short-salt teardown logs
        // a warning, which is the behaviour under test.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Salt read returned too few bytes"));
        r1.onCharacteristicRead(Refractometer::DiFluidR1::SALT,
                                QByteArray::fromHex("0102030405"));

        QVERIFY(!r1.isConnected());
        // Tearing down resets to Disconnected; no connectedChanged because
        // we never entered Ready.
        QCOMPARE(connSpy.count(), 0);
        QCOMPARE(r1.m_phase, DiFluidR1::Phase::Disconnected);
    }

    void onCharacteristicRead_validSaltPromotesToReady() {
        DiFluidR1 r1(nullptr);
        r1.m_phase = DiFluidR1::Phase::CharacteristicsReady;
        QSignalSpy connSpy(&r1, &RefractometerDevice::connectedChanged);

        r1.onCharacteristicRead(Refractometer::DiFluidR1::SALT, kSalt);

        QVERIFY(r1.isConnected());
        QCOMPARE(connSpy.count(), 1);
        QCOMPARE(r1.m_phase, DiFluidR1::Phase::Ready);
    }
};

QTEST_MAIN(tst_DiFluidR1)
#include "tst_difluidr1.moc"
