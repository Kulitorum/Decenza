#pragma once

#include <QByteArray>
#include <zlib.h>

namespace Gzip {

// gzip (RFC 1952) from the zlib that ships with QuaZip (cmake/quazip.cmake).
// Empty when the data cannot be compressed (empty input, or zlib failed).
inline QByteArray compress(const QByteArray& data)
{
    if (data.isEmpty())
        return {};
    z_stream zs{};
    // windowBits 15 + 16 selects the gzip wrapper instead of zlib's (zlib.h, deflateInit2).
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    // One Z_FINISH call into deflateBound() bytes always completes (zlib.h, deflateBound).
    QByteArray out(qsizetype(deflateBound(&zs, uLong(data.size()))), Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    zs.avail_in = uInt(data.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    out.resize(qsizetype(zs.total_out));
    deflateEnd(&zs);
    return rc == Z_STREAM_END ? out : QByteArray();
}

}  // namespace Gzip
