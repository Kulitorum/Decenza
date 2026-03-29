#include <QtTest>

#include "ble/protocol/binarycodec.h"

// Test BLE binary codec encode/decode round-trips and edge cases.
// Expected values derived from de1app encode_* / decode_* procs.
// BinaryCodec is pure math with no state — no mocks or friend access needed.

class tst_BinaryCodec : public QObject {
    Q_OBJECT

private slots:

    // ===== U8P4: 4 fractional bits, range 0-15.9375 (de1app encode_U8P4) =====

    void u8p4RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<double>("expected");
        QTest::addColumn<uint8_t>("expectedEncoded");

        QTest::newRow("zero")      << 0.0    << 0.0    << uint8_t(0x00);
        QTest::newRow("one")       << 1.0    << 1.0    << uint8_t(0x10);
        QTest::newRow("9bar")      << 9.0    << 9.0    << uint8_t(0x90);
        QTest::newRow("max exact") << 15.9375 << 15.9375 << uint8_t(0xFF);
        QTest::newRow("half step") << 0.5    << 0.5    << uint8_t(0x08);
        QTest::newRow("quarter")   << 0.25   << 0.25   << uint8_t(0x04);
    }

    void u8p4RoundTrip() {
        QFETCH(double, input);
        QFETCH(double, expected);
        QFETCH(uint8_t, expectedEncoded);

        uint8_t encoded = BinaryCodec::encodeU8P4(input);
        QCOMPARE(encoded, expectedEncoded);

        double decoded = BinaryCodec::decodeU8P4(encoded);
        QCOMPARE(decoded, expected);
    }

    void u8p4Clamping() {
        // Negative values clamp to 0
        QCOMPARE(BinaryCodec::encodeU8P4(-1.0), uint8_t(0));
        QCOMPARE(BinaryCodec::encodeU8P4(-100.0), uint8_t(0));

        // Values above 15.9375 clamp to 255
        QCOMPARE(BinaryCodec::encodeU8P4(16.0), uint8_t(255));
        QCOMPARE(BinaryCodec::encodeU8P4(20.0), uint8_t(255));
    }

    void u8p4Precision() {
        // 9.2 should encode within 1/16 resolution (0.0625)
        uint8_t encoded = BinaryCodec::encodeU8P4(9.2);
        double decoded = BinaryCodec::decodeU8P4(encoded);
        QVERIFY(qAbs(decoded - 9.2) <= 0.0625);
    }

    // ===== U8P1: 1 fractional bit, range 0-127.5 (de1app encode_U8P1) =====

    void u8p1RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<double>("expected");
        QTest::addColumn<uint8_t>("expectedEncoded");

        QTest::newRow("zero")   << 0.0    << 0.0    << uint8_t(0x00);
        QTest::newRow("93C")    << 93.0   << 93.0   << uint8_t(186);  // 93*2
        QTest::newRow("93.5C")  << 93.5   << 93.5   << uint8_t(187);  // 93.5*2
        QTest::newRow("127.5C") << 127.5  << 127.5  << uint8_t(255);  // max
    }

    void u8p1RoundTrip() {
        QFETCH(double, input);
        QFETCH(double, expected);
        QFETCH(uint8_t, expectedEncoded);

        uint8_t encoded = BinaryCodec::encodeU8P1(input);
        QCOMPARE(encoded, expectedEncoded);

        double decoded = BinaryCodec::decodeU8P1(encoded);
        QCOMPARE(decoded, expected);
    }

    void u8p1Clamping() {
        QCOMPARE(BinaryCodec::encodeU8P1(-5.0), uint8_t(0));
        // Values above 127.5 clamp to 255
        QCOMPARE(BinaryCodec::encodeU8P1(128.0), uint8_t(255));
        QCOMPARE(BinaryCodec::encodeU8P1(200.0), uint8_t(255));
    }

    // ===== U8P0: 8-bit integer, range 0-255 (de1app encode_U8P0) =====
    // This is the encoding used for TargetEspressoVol — bug #556 had it hardcoded to 36

    void u8p0RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<uint8_t>("expectedEncoded");

        QTest::newRow("zero")    << 0.0   << uint8_t(0);
        QTest::newRow("60s")     << 60.0  << uint8_t(60);
        QTest::newRow("200ml")   << 200.0 << uint8_t(200);   // TargetEspressoVol=200 (de1app espresso_typical_volume)
        QTest::newRow("255 max") << 255.0 << uint8_t(255);
    }

    void u8p0RoundTrip() {
        QFETCH(double, input);
        QFETCH(uint8_t, expectedEncoded);

        uint8_t encoded = BinaryCodec::encodeU8P0(input);
        QCOMPARE(encoded, expectedEncoded);

        double decoded = BinaryCodec::decodeU8P0(encoded);
        QCOMPARE(decoded, input);
    }

    void u8p0TargetEspressoVol200() {
        // Bug #556: TargetEspressoVol was hardcoded to 36 instead of 200
        // Verify 200 encodes to 0xC8 (de1app espresso_typical_volume = 200)
        uint8_t encoded = BinaryCodec::encodeU8P0(200.0);
        QCOMPARE(encoded, uint8_t(0xC8));
        QCOMPARE(BinaryCodec::decodeU8P0(encoded), 200.0);
    }

    void u8p0Clamping() {
        QCOMPARE(BinaryCodec::encodeU8P0(-1.0), uint8_t(0));
        // Values above 255 clamp to 255
        QCOMPARE(BinaryCodec::encodeU8P0(256.0), uint8_t(255));
        QCOMPARE(BinaryCodec::encodeU8P0(300.0), uint8_t(255));
    }

    // ===== U16P8: 8 fractional bits, range 0-255.996 (de1app encode_U16P8) =====

    void u16p8RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<uint16_t>("expectedEncoded");

        QTest::newRow("zero")  << 0.0   << uint16_t(0x0000);
        QTest::newRow("93C")   << 93.0  << uint16_t(93 * 256);   // 0x5D00
        QTest::newRow("93.5C") << 93.5  << uint16_t(23936);      // 93.5*256 = 23936
    }

    void u16p8RoundTrip() {
        QFETCH(double, input);
        QFETCH(uint16_t, expectedEncoded);

        uint16_t encoded = BinaryCodec::encodeU16P8(input);
        QCOMPARE(encoded, expectedEncoded);

        double decoded = BinaryCodec::decodeU16P8(encoded);
        QVERIFY(qAbs(decoded - input) < 1.0 / 256.0);
    }

    void u16p8Precision() {
        // Verify precision is within 1/256 (0.00390625)
        double value = 93.123;
        uint16_t encoded = BinaryCodec::encodeU16P8(value);
        double decoded = BinaryCodec::decodeU16P8(encoded);
        QVERIFY(qAbs(decoded - value) <= 1.0 / 256.0);
    }

    // ===== S32P16: signed 32-bit, 16 fractional bits (de1app encode_S32P16) =====

    void s32p16RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<int32_t>("expectedEncoded");

        QTest::newRow("zero")     << 0.0      << int32_t(0);
        QTest::newRow("one")      << 1.0      << int32_t(65536);    // 1.0 * 65536
        QTest::newRow("negative") << -1.0     << int32_t(-65536);
        QTest::newRow("large")    << 100.0    << int32_t(100 * 65536);  // 6,553,600
    }

    void s32p16RoundTrip() {
        QFETCH(double, input);
        QFETCH(int32_t, expectedEncoded);

        int32_t encoded = BinaryCodec::encodeS32P16(input);
        QCOMPARE(encoded, expectedEncoded);

        double decoded = BinaryCodec::decodeS32P16(encoded);
        QVERIFY(qAbs(decoded - input) <= 1.0 / 65536.0);
    }

    // ===== F8_1_7: custom float for frame duration (de1app encode_F8_1_7) =====
    // Short durations (<12.75): 0.1s precision, value * 10
    // Long durations (>=12.75): 1s precision, value | 0x80

    void f8_1_7RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<uint8_t>("expectedEncoded");
        QTest::addColumn<double>("expectedDecoded");

        // Short mode: value < 12.75, encoded = round(value * 10)
        QTest::newRow("zero")   << 0.0   << uint8_t(0)    << 0.0;
        QTest::newRow("5s")     << 5.0   << uint8_t(50)   << 5.0;
        QTest::newRow("10s")    << 10.0  << uint8_t(100)  << 10.0;
        QTest::newRow("12.7s")  << 12.7  << uint8_t(127)  << 12.7;

        // Long mode: value >= 12.75, encoded = round(value) | 0x80
        QTest::newRow("13s")    << 13.0  << uint8_t(13 | 0x80) << 13.0;
        QTest::newRow("30s")    << 30.0  << uint8_t(30 | 0x80) << 30.0;
        QTest::newRow("60s")    << 60.0  << uint8_t(60 | 0x80) << 60.0;
        QTest::newRow("127s")   << 127.0 << uint8_t(127 | 0x80) << 127.0;
    }

    void f8_1_7RoundTrip() {
        QFETCH(double, input);
        QFETCH(uint8_t, expectedEncoded);
        QFETCH(double, expectedDecoded);

        uint8_t encoded = BinaryCodec::encodeF8_1_7(input);
        QCOMPARE(encoded, expectedEncoded);

        double decoded = BinaryCodec::decodeF8_1_7(encoded);
        QCOMPARE(decoded, expectedDecoded);
    }

    void f8_1_7Boundary() {
        // 12.74 stays in short mode, 12.75 switches to long mode
        uint8_t shortMode = BinaryCodec::encodeF8_1_7(12.74);
        QVERIFY((shortMode & 0x80) == 0);  // High bit NOT set

        uint8_t longMode = BinaryCodec::encodeF8_1_7(12.75);
        QVERIFY((longMode & 0x80) != 0);   // High bit IS set

        // Short mode: 12.7 → 127 → decode to 12.7
        QCOMPARE(BinaryCodec::decodeF8_1_7(127), 12.7);

        // Long mode: 0x80 | 13 = 0x8D → decode to 13.0
        QCOMPARE(BinaryCodec::decodeF8_1_7(0x8D), 13.0);
    }

    void f8_1_7ShortPrecision() {
        // In short mode, precision is 0.1s
        uint8_t enc = BinaryCodec::encodeF8_1_7(5.3);
        double dec = BinaryCodec::decodeF8_1_7(enc);
        QVERIFY(qAbs(dec - 5.3) <= 0.05);
    }

    // ===== U10P0: 10-bit with flag bit (de1app volume limit in shot frames) =====

    void u10p0FlagBit() {
        // Encode 0 should still set bit 10 (0x0400)
        uint16_t encoded = BinaryCodec::encodeU10P0(0.0);
        QCOMPARE(encoded, uint16_t(0x0400));

        // Decode should strip the flag bit
        QCOMPARE(BinaryCodec::decodeU10P0(0x0400), 0.0);
    }

    void u10p0RoundTrip_data() {
        QTest::addColumn<double>("input");
        QTest::addColumn<double>("expectedDecoded");

        QTest::newRow("zero") << 0.0    << 0.0;
        QTest::newRow("100")  << 100.0  << 100.0;
        QTest::newRow("500")  << 500.0  << 500.0;
        QTest::newRow("1023") << 1023.0 << 1023.0;  // Max 10-bit value
    }

    void u10p0RoundTrip() {
        QFETCH(double, input);
        QFETCH(double, expectedDecoded);

        uint16_t encoded = BinaryCodec::encodeU10P0(input);
        QVERIFY(encoded & 0x0400);  // Flag bit always set

        double decoded = BinaryCodec::decodeU10P0(encoded);
        QCOMPARE(decoded, expectedDecoded);
    }

    // ===== U24P0: 24-bit big-endian integer =====

    void u24p0Endianness() {
        QByteArray encoded = BinaryCodec::encodeU24P0(0x123456);
        QCOMPARE(encoded.size(), 3);
        QCOMPARE(uint8_t(encoded[0]), uint8_t(0x12));
        QCOMPARE(uint8_t(encoded[1]), uint8_t(0x34));
        QCOMPARE(uint8_t(encoded[2]), uint8_t(0x56));
    }

    void u24p0RoundTrip() {
        uint32_t value = 0xABCDEF;
        QByteArray encoded = BinaryCodec::encodeU24P0(value);
        uint32_t decoded = BinaryCodec::decodeU24P0(encoded);
        QCOMPARE(decoded, value);
    }

    void u24p0ThreeCharOverload() {
        uint32_t decoded = BinaryCodec::decodeU24P0(0x12, 0x34, 0x56);
        QCOMPARE(decoded, uint32_t(0x123456));
    }

    void u24p0EmptyBuffer() {
        QCOMPARE(BinaryCodec::decodeU24P0(QByteArray()), uint32_t(0));
    }

    void u24p0ShortBuffer() {
        QCOMPARE(BinaryCodec::decodeU24P0(QByteArray(2, 0)), uint32_t(0));
    }

    // ===== U32P0: 32-bit big-endian integer =====

    void u32p0Endianness() {
        QByteArray encoded = BinaryCodec::encodeU32P0(0xDEADBEEF);
        QCOMPARE(encoded.size(), 4);
        QCOMPARE(uint8_t(encoded[0]), uint8_t(0xDE));
        QCOMPARE(uint8_t(encoded[1]), uint8_t(0xAD));
        QCOMPARE(uint8_t(encoded[2]), uint8_t(0xBE));
        QCOMPARE(uint8_t(encoded[3]), uint8_t(0xEF));
    }

    void u32p0RoundTrip() {
        uint32_t value = 0x12345678;
        QByteArray encoded = BinaryCodec::encodeU32P0(value);
        uint32_t decoded = BinaryCodec::decodeU32P0(encoded);
        QCOMPARE(decoded, value);
    }

    void u32p0EmptyBuffer() {
        QCOMPARE(BinaryCodec::decodeU32P0(QByteArray()), uint32_t(0));
    }

    void u32p0ShortBuffer() {
        QCOMPARE(BinaryCodec::decodeU32P0(QByteArray(3, 0)), uint32_t(0));
    }

    // ===== decode3CharToU24P16: shot sample parsing =====

    void decode3CharToU24P16KnownValues() {
        // Integer part = char1, fractional = char2/256 + char3/65536
        QCOMPARE(BinaryCodec::decode3CharToU24P16(93, 0, 0), 93.0);
        QCOMPARE(BinaryCodec::decode3CharToU24P16(0, 128, 0), 0.5);  // 128/256
        QCOMPARE(BinaryCodec::decode3CharToU24P16(0, 0, 0), 0.0);

        // 93 + 128/256 + 0/65536 = 93.5
        QCOMPARE(BinaryCodec::decode3CharToU24P16(93, 128, 0), 93.5);
    }

    // ===== ShortBE: 16-bit big-endian =====

    void shortBERoundTrip() {
        uint16_t value = 0x1234;
        QByteArray encoded = BinaryCodec::encodeShortBE(value);
        QCOMPARE(encoded.size(), 2);
        QCOMPARE(uint8_t(encoded[0]), uint8_t(0x12));
        QCOMPARE(uint8_t(encoded[1]), uint8_t(0x34));

        uint16_t decoded = BinaryCodec::decodeShortBE(encoded, 0);
        QCOMPARE(decoded, value);
    }

    void shortBEWithOffset() {
        // 4-byte buffer, decode at offset 2
        QByteArray buf(4, 0);
        buf[2] = char(0xAB);
        buf[3] = char(0xCD);
        QCOMPARE(BinaryCodec::decodeShortBE(buf, 2), uint16_t(0xABCD));
    }

    void shortBEShortBuffer() {
        QCOMPARE(BinaryCodec::decodeShortBE(QByteArray(1, 0), 0), uint16_t(0));
    }

    // ===== SignedShortBE =====

    void signedShortBENegative() {
        // -100 in 16-bit two's complement = 0xFF9C
        QByteArray encoded(2, 0);
        encoded[0] = char(0xFF);
        encoded[1] = char(0x9C);
        int16_t decoded = BinaryCodec::decodeSignedShortBE(encoded, 0);
        QCOMPARE(decoded, int16_t(-100));
    }

    void signedShortBEPositive() {
        QByteArray encoded = BinaryCodec::encodeShortBE(500);
        int16_t decoded = BinaryCodec::decodeSignedShortBE(encoded, 0);
        QCOMPARE(decoded, int16_t(500));
    }

    void signedShortBEZero() {
        QByteArray encoded = BinaryCodec::encodeShortBE(0);
        int16_t decoded = BinaryCodec::decodeSignedShortBE(encoded, 0);
        QCOMPARE(decoded, int16_t(0));
    }

    void signedShortBEShortBuffer() {
        QCOMPARE(BinaryCodec::decodeSignedShortBE(QByteArray(1, 0), 0), int16_t(0));
    }
};

QTEST_GUILESS_MAIN(tst_BinaryCodec)
#include "tst_binarycodec.moc"
