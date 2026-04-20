#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "core/firmwareheader.h"

// Tests the pure firmware-file header parser and the on-disk file validator.
//
// parseHeader() is a pure QByteArray → struct transformer and has no file
// I/O; validateFile() checks an on-disk .dat against the rules in the
// firmware-update spec (Requirement: Firmware download and validation).
//
// No network, no BLE. Fixture .dat files are generated in a QTemporaryDir
// so the tests carry no binary artifacts in the repo.

namespace {

// Pack a little-endian u32 into a QByteArray at the given offset.
void packU32LE(QByteArray& buf, int offset, uint32_t value) {
    buf[offset]     = static_cast<char>(value        & 0xFF);
    buf[offset + 1] = static_cast<char>((value >> 8)  & 0xFF);
    buf[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
    buf[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

// Build a 64-byte header with the given numeric fields and an opaque IV of
// sequential bytes (0x00, 0x01, 0x02, ...). Mirrors the layout of a real
// bootfwupdate.dat header from decentespresso/de1app main.
QByteArray makeHeader(uint32_t checksum, uint32_t boardMarker, uint32_t version,
                      uint32_t byteCount, uint32_t cpuBytes = 0,
                      uint32_t dcSum = 0, uint32_t headerChecksum = 0) {
    QByteArray header(DE1::Firmware::HEADER_SIZE, char(0));
    packU32LE(header, 0,  checksum);
    packU32LE(header, 4,  boardMarker);
    packU32LE(header, 8,  version);
    packU32LE(header, 12, byteCount);
    packU32LE(header, 16, cpuBytes);
    // offset 20 Unused stays zero
    packU32LE(header, 24, dcSum);
    // offsets 28-59: IV = 0x00, 0x01, ..., 0x1F
    for (int i = 0; i < 32; ++i) {
        header[28 + i] = static_cast<char>(i);
    }
    packU32LE(header, 60, headerChecksum);
    return header;
}

// Write a synthetic .dat file: 64-byte header plus `payloadSize` bytes of
// zero payload. Returns the path.
QString writeSyntheticDat(const QTemporaryDir& dir, const QByteArray& header,
                          qsizetype payloadSize) {
    const QString path = dir.filePath(QStringLiteral("synthetic.dat"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return QString();
    }
    f.write(header);
    f.write(QByteArray(payloadSize, char(0)));
    f.close();
    return path;
}

}  // namespace

class tst_FirmwareHeader : public QObject {
    Q_OBJECT

private slots:

    // ===== parseHeader: pure byte → struct =====

    void parseHeader_valid() {
        QByteArray bytes = makeHeader(
            /*checksum*/       0x85E18D15,
            /*boardMarker*/    DE1::Firmware::BOARD_MARKER,
            /*version*/        1352,
            /*byteCount*/      461824,
            /*cpuBytes*/       298080,
            /*dcSum*/          0xF9D591B1,
            /*headerChecksum*/ 0x17F3560B);

        auto parsed = DE1::Firmware::parseHeader(bytes);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->checksum,       uint32_t(0x85E18D15));
        QCOMPARE(parsed->boardMarker,    DE1::Firmware::BOARD_MARKER);
        QCOMPARE(parsed->version,        uint32_t(1352));
        QCOMPARE(parsed->byteCount,      uint32_t(461824));
        QCOMPARE(parsed->cpuBytes,       uint32_t(298080));
        QCOMPARE(parsed->unused,         uint32_t(0));
        QCOMPARE(parsed->dcSum,          uint32_t(0xF9D591B1));
        QCOMPARE(parsed->headerChecksum, uint32_t(0x17F3560B));
    }

    void parseHeader_preservesIv() {
        // IV bytes must be preserved verbatim — Decenza never interprets
        // them, but truncating or byte-swapping would break the later
        // full-file upload (DE1 firmware does the decryption).
        QByteArray bytes = makeHeader(0, DE1::Firmware::BOARD_MARKER, 1, 0);
        auto parsed = DE1::Firmware::parseHeader(bytes);
        QVERIFY(parsed.has_value());
        for (int i = 0; i < 32; ++i) {
            QCOMPARE(parsed->iv[i], uint8_t(i));
        }
    }

    void parseHeader_rejectsShortBuffer() {
        // A buffer shorter than HEADER_SIZE is not a valid header. Must
        // return nullopt rather than read past the end of the buffer.
        QCOMPARE(DE1::Firmware::parseHeader(QByteArray()).has_value(),              false);
        QCOMPARE(DE1::Firmware::parseHeader(QByteArray(63, char(0))).has_value(),   false);
        QCOMPARE(DE1::Firmware::parseHeader(QByteArray(1,  char(0))).has_value(),   false);
    }

    void parseHeader_acceptsExactAndLonger() {
        // Exactly 64 bytes — OK. Longer input (the entire .dat file in
        // memory, say) — also OK; we read only the first 64 bytes.
        QByteArray headerOnly = makeHeader(0, DE1::Firmware::BOARD_MARKER, 1, 0);
        QByteArray headerPlusPayload = headerOnly + QByteArray(1024, char(0xFF));

        QVERIFY(DE1::Firmware::parseHeader(headerOnly).has_value());
        QVERIFY(DE1::Firmware::parseHeader(headerPlusPayload).has_value());
    }

    // ===== validateFile: on-disk checks =====

    void validateFile_ok() {
        // A .dat with correct BoardMarker and fileSize == ByteCount + HEADER_SIZE
        // is accepted.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const qsizetype payloadSize = 256;
        QByteArray header = makeHeader(0, DE1::Firmware::BOARD_MARKER, 1352,
                                       uint32_t(payloadSize));
        QString path = writeSyntheticDat(dir, header, payloadSize);

        auto result = DE1::Firmware::validateFile(path);
        QCOMPARE(result.status, DE1::Firmware::Validation::Ok);
        QCOMPARE(result.header.boardMarker, DE1::Firmware::BOARD_MARKER);
        QCOMPARE(result.header.version,     uint32_t(1352));
        QCOMPARE(result.header.byteCount,   uint32_t(payloadSize));
    }

    void validateFile_badBoardMarker() {
        // Wrong BoardMarker means the file isn't a DE1 firmware image at
        // all — this is non-retryable: retrying won't help, and the flow
        // should disable itself until the next app restart.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray header = makeHeader(0, /*boardMarker*/ 0xCAFEBABE, 1, 0);
        QString path = writeSyntheticDat(dir, header, 0);

        auto result = DE1::Firmware::validateFile(path);
        QCOMPARE(result.status, DE1::Firmware::Validation::BadBoardMarker);
        QVERIFY(!result.errorDetail.isEmpty());
    }

    void validateFile_truncated() {
        // fileSize < ByteCount + HEADER_SIZE — classic partial download.
        // Retryable: next download may well complete.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray header = makeHeader(0, DE1::Firmware::BOARD_MARKER, 1,
                                       /*byteCount*/ 1024);
        QString path = writeSyntheticDat(dir, header, /*payloadSize*/ 256);

        auto result = DE1::Firmware::validateFile(path);
        QCOMPARE(result.status, DE1::Firmware::Validation::Truncated);
    }

    void validateFile_tooShortHeader() {
        // File doesn't even contain a full 64-byte header.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("tiny.dat"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(32, char(0)));
        f.close();

        auto result = DE1::Firmware::validateFile(path);
        QCOMPARE(result.status, DE1::Firmware::Validation::TooShortHeader);
    }

    void validateFile_unreadable() {
        // Non-existent path must not crash; must be classified so the
        // caller can decide between retry and surface-as-error.
        auto result = DE1::Firmware::validateFile(
            QStringLiteral("Z:/definitely/does/not/exist/abc.dat"));
        QCOMPARE(result.status, DE1::Firmware::Validation::UnreadableFile);
    }

    // ===== BOARD_MARKER constant sanity =====

    void boardMarkerConstant() {
        // Cross-checked against Kal Freese's Python updater
        // (github.com/kalfreese/de1-firmware-updater) and observed live at
        // offset 4 of bootfwupdate.dat on decentespresso/de1app main.
        QCOMPARE(DE1::Firmware::BOARD_MARKER, uint32_t(0xDE100001));
    }
};

QTEST_GUILESS_MAIN(tst_FirmwareHeader)
#include "tst_firmwareheader.moc"
