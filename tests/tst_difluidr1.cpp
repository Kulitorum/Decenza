#include <QtTest>

#include "ble/refractometers/difluidr1.h"

// Test DiFluid R1 refractometer protocol pieces against the bit-exact vectors
// from issue #1307 (reverse-engineered from official app v1.2.6):
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

namespace {

QByteArray hexToBytes(const char* hex) {
    return QByteArray::fromHex(QByteArray(hex).replace(' ', ""));
}

} // namespace

class tst_DiFluidR1 : public QObject {
    Q_OBJECT

private slots:
    void deriveKey_matchesIssueVector() {
        const QByteArray salt = hexToBytes("B8 D6 1B B5 DC 1A 03 8A 06 5E 15 09");
        const auto key = DiFluidR1::deriveKey(salt);

        QByteArray actual(reinterpret_cast<const char*>(key.data()), 16);
        const QByteArray expected = hexToBytes("B8 D6 1A B4 DA 18 AA AA AA AA AA AA AA AA AA AA");
        QCOMPARE(actual, expected);
    }

    void decryptFrame_matchesIssueVector() {
        const QByteArray salt = hexToBytes("B8 D6 1B B5 DC 1A 03 8A 06 5E 15 09");
        const auto key = DiFluidR1::deriveKey(salt);

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
        const QByteArray salt = hexToBytes("B8 D6 1B B5 DC 1A 03 8A 06 5E 15 09");
        const auto key = DiFluidR1::deriveKey(salt);

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

    void isR1Device_recognizesPrefix() {
        QVERIFY(DiFluidR1::isR1Device("DFT_TDJ_301033"));
        QVERIFY(DiFluidR1::isR1Device("dft_tdj_001"));  // case-insensitive
        QVERIFY(!DiFluidR1::isR1Device("R2 Extract"));
        QVERIFY(!DiFluidR1::isR1Device("DiFluid R2"));
        QVERIFY(!DiFluidR1::isR1Device("difluid"));      // microbalance scale
        QVERIFY(!DiFluidR1::isR1Device(""));
    }
};

QTEST_MAIN(tst_DiFluidR1)
#include "tst_difluidr1.moc"
