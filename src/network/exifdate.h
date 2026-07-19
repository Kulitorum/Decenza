#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>

// Minimal EXIF DateTimeOriginal scanner for JPEG bytes.
//
// Split out of ShotServer::extractImageDate so it can be tested: it is pure
// (bytes in, date out) while its caller needs a running server, a storage
// backend, a DE1 device and an AI manager to link. That asymmetry is why this
// logic went untested for its whole life, and it is not a hypothetical cost —
// a half-applied rename in the caller (`dt` vs `parsedDt`) had made this entire
// fallback path dead code, and nothing noticed, because there was no test that
// could run without standing up a web server.
//
// Scope, so nobody mistakes this for an EXIF library: it does not parse the
// TIFF/IFD structure. It finds the APP1 segment, confirms the "Exif\0\0"
// header, and regex-searches for the first "YYYY:MM:DD HH:MM:SS" string in it.
// That is deliberately loose — DateTimeOriginal, DateTimeDigitized and
// ModifyDate all share the format, and this takes whichever appears first.
// Good enough to date a photo, and the caller only reaches it when exiftool
// (which does parse properly) is unavailable or came up empty.

namespace ExifDate {

// Scans JPEG bytes for an EXIF date. Returns an invalid QDateTime when the
// data is not a JPEG, carries no APP1/Exif segment, or holds no plausible
// date. Years outside 1990..2100 are rejected as parse noise rather than
// dates — the regex will happily match unrelated binary that looks numeric.
inline QDateTime fromJpegBytes(const QByteArray& data)
{
    // JPEG magic (SOI marker).
    if (data.size() < 4 || static_cast<uchar>(data[0]) != 0xFF
        || static_cast<uchar>(data[1]) != 0xD8) {
        return QDateTime();
    }

    int pos = 2;
    while (pos < data.size() - 4) {
        if (static_cast<uchar>(data[pos]) != 0xFF) {
            pos++;
            continue;
        }

        const uchar marker = static_cast<uchar>(data[pos + 1]);
        if (marker == 0xE1) {  // APP1 — where EXIF lives
            const int length = (static_cast<uchar>(data[pos + 2]) << 8)
                             | static_cast<uchar>(data[pos + 3]);
            // A truncated read (the caller only loads the first 64 KB) can
            // leave `length` pointing past the buffer; mid() clamps, so a
            // partial segment is scanned rather than treated as absent.
            const QByteArray exifData = data.mid(pos + 4, length - 2);

            if (exifData.startsWith(QByteArrayLiteral("Exif\0\0"))) {
                const QString exifStr = QString::fromLatin1(exifData);
                static const QRegularExpression dateRe(
                    QStringLiteral("(\\d{4}):(\\d{2}):(\\d{2}) "
                                   "(\\d{2}):(\\d{2}):(\\d{2})"));
                const QRegularExpressionMatch match = dateRe.match(exifStr);
                if (match.hasMatch()) {
                    const int year = match.captured(1).toInt();
                    const QDateTime parsed(
                        QDate(year, match.captured(2).toInt(),
                              match.captured(3).toInt()),
                        QTime(match.captured(4).toInt(),
                              match.captured(5).toInt(),
                              match.captured(6).toInt()));
                    if (parsed.isValid() && year >= 1990 && year <= 2100)
                        return parsed;
                }
            }
            break;
        }
        if (marker == 0xD9 || marker == 0xDA)
            break;  // End of image, or start of scan — no EXIF ahead.
        if (marker >= 0xE0 && marker <= 0xEF) {
            const int length = (static_cast<uchar>(data[pos + 2]) << 8)
                             | static_cast<uchar>(data[pos + 3]);
            // length is two bytes, so 0..65535 — this always advances by at
            // least 2 and the loop cannot spin, even on malformed input.
            pos += 2 + length;
        } else {
            pos += 2;
        }
    }

    return QDateTime();
}

}  // namespace ExifDate
