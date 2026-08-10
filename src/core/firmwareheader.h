#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QString>

// DE1 bootfwupdate.dat header parser and on-disk validator.
//
// The file header is 64 bytes: seven u32 numeric fields (little-endian)
// followed by a 32-byte payload-decryption IV and a final u32 header
// checksum. Fields 0..27 match de1app's binary.tcl:379-390 spec; the IV
// and HeaderChecksum (offsets 28-59 and 60-63) were added to support
// encrypted payloads and are currently opaque to Decenza.
//
// Client-side validation covers BoardMarker equality, the on-disk file
// size against ByteCount (both a floor and a ceiling), and a set of
// structural invariants on the remaining header fields — see
// validateFile(). The CheckSum, DCSum and HeaderChecksum *algorithms* are
// pending a protocol question to Decent (see TODO(firmware-crc)), so those
// fields can only be asserted non-empty, not recomputed; the DE1's own
// verify-phase response (FirstError == {0xFF, 0xFF, 0xFD}) remains the
// authoritative correctness gate for the flashed image.

namespace DE1::Firmware {

// BoardMarker value expected at offset 4 of a valid DE1 firmware file.
// Cross-checked against Kal Freese's Python updater and the live file
// on decentespresso/de1app main.
inline constexpr uint32_t BOARD_MARKER = 0xDE100001;

// Byte size of the on-disk firmware header.
inline constexpr qsizetype HEADER_SIZE = 64;

struct Header {
    uint32_t checksum       = 0;         // offset  0 — algorithm pending
    uint32_t boardMarker    = 0;         // offset  4 — must equal BOARD_MARKER
    uint32_t version        = 0;         // offset  8 — linear build number
    uint32_t byteCount      = 0;         // offset 12 — payload size
    uint32_t cpuBytes       = 0;         // offset 16
    uint32_t unused         = 0;         // offset 20 — always zero
    uint32_t dcSum          = 0;         // offset 24 — algorithm pending
    std::array<uint8_t, 32> iv{};        // offset 28 — payload IV, opaque
    uint32_t headerChecksum = 0;         // offset 60 — algorithm pending
};

namespace detail {
    inline uint32_t readU32LE(const QByteArray& bytes, int offset) {
        return  static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset])) |
               (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8) |
               (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3])) << 24);
    }
}  // namespace detail

// Parse the first HEADER_SIZE bytes of `bytes` into a Header. Returns
// std::nullopt when the buffer is shorter than HEADER_SIZE. Reads only
// the first HEADER_SIZE bytes; additional data is ignored.
inline std::optional<Header> parseHeader(const QByteArray& bytes) {
    if (bytes.size() < HEADER_SIZE) {
        return std::nullopt;
    }
    Header h;
    h.checksum       = detail::readU32LE(bytes,  0);
    h.boardMarker    = detail::readU32LE(bytes,  4);
    h.version        = detail::readU32LE(bytes,  8);
    h.byteCount      = detail::readU32LE(bytes, 12);
    h.cpuBytes       = detail::readU32LE(bytes, 16);
    h.unused         = detail::readU32LE(bytes, 20);
    h.dcSum          = detail::readU32LE(bytes, 24);
    for (int i = 0; i < 32; ++i) {
        h.iv[i] = static_cast<uint8_t>(bytes[28 + i]);
    }
    h.headerChecksum = detail::readU32LE(bytes, 60);
    return h;
}

// Classification of a file-validation outcome. UnreadableFile, TooShortHeader,
// and Truncated are transient (retryable); BadBoardMarker is permanent
// (non-retryable, disable the flow until app restart).
enum class Validation {
    Ok,
    UnreadableFile,
    TooShortHeader,
    BadBoardMarker,
    Truncated,
    MalformedHeader
};

struct ValidationResult {
    Validation status = Validation::Ok;
    Header     header{};
    QString    errorDetail;
};

// Validate an on-disk bootfwupdate.dat. Reads the first HEADER_SIZE bytes,
// parses them, and checks BoardMarker, the file size against ByteCount in
// both directions, and the structural invariants listed at the bottom of
// the function. Does NOT recompute the opaque checksum fields — those await
// a protocol answer from Decent (TODO(firmware-crc)) — but does reject them
// when zero, since no published image leaves them empty.
inline ValidationResult validateFile(const QString& path) {
    ValidationResult result;

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.status = Validation::UnreadableFile;
        result.errorDetail = QStringLiteral("File does not exist: %1").arg(path);
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.status = Validation::UnreadableFile;
        result.errorDetail = QStringLiteral("Cannot open firmware file: %1").arg(file.errorString());
        return result;
    }

    QByteArray headerBytes = file.read(HEADER_SIZE);
    if (headerBytes.size() < HEADER_SIZE) {
        result.status = Validation::TooShortHeader;
        result.errorDetail = QStringLiteral(
            "Firmware file is smaller than the 64-byte header (got %1 bytes)"
        ).arg(headerBytes.size());
        return result;
    }

    auto parsed = parseHeader(headerBytes);
    // parseHeader can only fail on a short buffer, which we already excluded.
    result.header = parsed.value();

    if (result.header.boardMarker != BOARD_MARKER) {
        result.status = Validation::BadBoardMarker;
        result.errorDetail = QStringLiteral(
            "Invalid DE1 firmware file: BoardMarker 0x%1 (expected 0x%2)"
        ).arg(result.header.boardMarker, 8, 16, QLatin1Char('0')).toUpper()
         .arg(BOARD_MARKER,            8, 16, QLatin1Char('0')).toUpper();
        return result;
    }

    const qint64 expected = qint64(result.header.byteCount) + HEADER_SIZE;
    if (info.size() < expected) {
        result.status = Validation::Truncated;
        result.errorDetail = QStringLiteral(
            "Firmware file truncated: have %1 bytes, need at least %2 (ByteCount + header)"
        ).arg(info.size()).arg(expected);
        return result;
    }

    // Upper bound as well as a floor. A file LONGER than its own header
    // declares is the signature of a resume the server answered with the
    // whole body instead of the requested range: the partial's bytes with a
    // complete copy appended. Published images carry a small trailing pad
    // (nightly v1352 is 463,872 bytes against a ByteCount of 461,824, so
    // 1,984 bytes past ByteCount + 64), so the ceiling has to allow slack —
    // but not a second image's worth. A whole duplicate body is orders of
    // magnitude past this.
    constexpr qint64 MAX_TRAILING_SLACK = 64 * 1024;
    if (info.size() > expected + MAX_TRAILING_SLACK) {
        result.status = Validation::MalformedHeader;
        result.errorDetail = QStringLiteral(
            "Firmware file oversized: %1 bytes against a declared %2 "
            "(ByteCount + header). A body was appended to a partial download."
        ).arg(info.size()).arg(expected);
        return result;
    }

    // Internal-consistency checks, adopted from reaprime/decaid's
    // FirmwareValidator. They cost nothing and reject a file whose header is
    // structurally impossible — which BoardMarker plus a size floor does not,
    // since BoardMarker sits at offset 4 and is identical in every DE1 image
    // ever published. A file spliced from two revisions passes both of the
    // checks above unchanged.
    const auto& h = result.header;
    QString malformed;
    if (h.byteCount == 0) {
        malformed = QStringLiteral("ByteCount is zero");
    } else if (h.cpuBytes == 0 || h.cpuBytes > h.byteCount) {
        malformed = QStringLiteral("CpuBytes %1 is not within ByteCount %2")
                        .arg(h.cpuBytes).arg(h.byteCount);
    } else if (h.unused != 0) {
        malformed = QStringLiteral("reserved header field is %1, expected 0").arg(h.unused);
    } else if (h.checksum == 0 || h.dcSum == 0 || h.headerChecksum == 0) {
        // The algorithms are undocumented (TODO(firmware-crc)), so we cannot
        // recompute these — but a real image never leaves them zero, so an
        // all-zero field still means the header is not a published one.
        malformed = QStringLiteral("checksum fields are incomplete");
    } else if (std::all_of(h.iv.begin(), h.iv.end(), [](uint8_t b) { return b == 0; })) {
        malformed = QStringLiteral("payload IV is all zeroes");
    }
    if (!malformed.isEmpty()) {
        result.status = Validation::MalformedHeader;
        result.errorDetail = QStringLiteral("Invalid DE1 firmware header: %1").arg(malformed);
        return result;
    }

    result.status = Validation::Ok;
    return result;
}

}  // namespace DE1::Firmware
