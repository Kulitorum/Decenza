#pragma once

#include <QByteArray>
#include <QtEndian>
#include <array>

// gzip (RFC 1952) built from qCompress's output, so no zlib dependency of our
// own: qCompress writes a 4-byte big-endian length and then a zlib stream
// (2-byte header, the deflate data, 4-byte Adler-32) from deflateInit()
// (qtbase/src/corelib/text/qbytearray.cpp:637-686). gzip is the same deflate
// data between a different header and trailer.
namespace Gzip {

namespace detail {

// CRC-32 (IEEE 802.3, reflected 0xEDB88320), the checksum RFC 1952 trails with.
constexpr std::array<quint32, 256> crcTable()
{
    std::array<quint32, 256> table{};
    for (quint32 n = 0; n < 256; ++n) {
        quint32 c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[n] = c;
    }
    return table;
}

inline quint32 crc32(const QByteArray& data)
{
    static constexpr std::array<quint32, 256> table = crcTable();
    quint32 c = 0xFFFFFFFFu;
    for (const char byte : data)
        c = table[(c ^ quint8(byte)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace detail

// Empty when the data cannot be compressed (empty input, or qCompress failed).
inline QByteArray compress(const QByteArray& data)
{
    const QByteArray zlib = qCompress(data);
    constexpr qsizetype kPrefix = 4 + 2;   // length, then the zlib header
    constexpr qsizetype kAdler = 4;
    if (data.isEmpty() || zlib.size() <= kPrefix + kAdler)
        return {};
    // ID1 ID2, CM = deflate, no flags, no mtime, no extra flags, OS unknown.
    static constexpr char header[10] = {'\x1f', '\x8b', 8, 0, 0, 0, 0, 0, 0, '\xff'};
    char trailer[8];
    qToLittleEndian(detail::crc32(data), trailer);
    qToLittleEndian(quint32(data.size()), trailer + 4);   // ISIZE is the size mod 2^32

    QByteArray out;
    out.reserve(sizeof(header) + zlib.size() - kPrefix - kAdler + sizeof(trailer));
    out.append(header, sizeof(header));
    out.append(zlib.constData() + kPrefix, zlib.size() - kPrefix - kAdler);
    out.append(trailer, sizeof(trailer));
    return out;
}

}  // namespace Gzip
