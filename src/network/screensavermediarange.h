#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace ScreensaverMediaRange {

constexpr qint64 socketHighWaterBytes = 256 * 1024;
constexpr qint64 mediaChunkBytes = 64 * 1024;

inline qint64 nextChunkSize(qint64 queuedBytes, qint64 remainingBytes)
{
    if (queuedBytes < 0 || queuedBytes >= socketHighWaterBytes || remainingBytes <= 0)
        return 0;
    return qMin(mediaChunkBytes, qMin(remainingBytes, socketHighWaterBytes - queuedBytes));
}

struct Range {
    qint64 first = 0;
    qint64 last = -1;
    bool partial = false;
    qint64 length() const { return last >= first ? last - first + 1 : 0; }
};

// Parses one RFC 9110 byte range. Multiple ranges and malformed/unsatisfiable
// requests are rejected instead of allocating a multipart response.
inline bool parse(const QByteArray& header, qint64 fileSize, Range* result)
{
    if (!result || fileSize <= 0)
        return false;
    if (header.trimmed().isEmpty()) {
        *result = {0, fileSize - 1, false};
        return true;
    }
    const QByteArray value = header.trimmed();
    if (value.size() < 6
        || value.left(6).compare(QByteArrayLiteral("bytes="), Qt::CaseInsensitive) != 0)
        return false;
    const QByteArray spec = value.mid(6);
    if (spec.contains(','))
        return false;
    const qsizetype dash = spec.indexOf('-');
    if (dash < 0 || spec.indexOf('-', dash + 1) >= 0)
        return false;

    bool firstOk = false;
    bool lastOk = false;
    const QByteArray left = spec.left(dash).trimmed();
    const QByteArray right = spec.mid(dash + 1).trimmed();
    const auto digitsOnly = [](const QByteArray& text) {
        if (text.isEmpty())
            return false;
        for (const char ch : text) {
            if (ch < '0' || ch > '9')
                return false;
        }
        return true;
    };
    qint64 first = 0;
    qint64 last = 0;
    if (left.isEmpty()) {
        if (!digitsOnly(right))
            return false;
        const qint64 suffix = right.toLongLong(&lastOk);
        if (!lastOk || suffix <= 0)
            return false;
        first = suffix >= fileSize ? 0 : fileSize - suffix;
        last = fileSize - 1;
    } else {
        if (!digitsOnly(left) || (!right.isEmpty() && !digitsOnly(right)))
            return false;
        first = left.toLongLong(&firstOk);
        if (!firstOk || first < 0 || first >= fileSize)
            return false;
        if (right.isEmpty()) {
            last = fileSize - 1;
        } else {
            last = right.toLongLong(&lastOk);
            if (!lastOk || last < first)
                return false;
            last = qMin(last, fileSize - 1);
        }
    }
    *result = {first, last, true};
    return true;
}

} // namespace ScreensaverMediaRange
