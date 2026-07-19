// Tests for the EXIF date scanner split out of ShotServer::extractImageDate.
//
// This logic shipped untested because its only caller needed a running web
// server, a storage backend, a DE1 device and an AI manager to link — so the
// cheap thing to do was nothing, and nothing is what was done. It cost: a
// half-applied variable rename (`dt` vs `parsedDt`) silently turned the whole
// fallback path into dead code, and it was found by reading the diff, not by
// any check. These tests are the check.

#include <QtTest>
#include <QByteArray>
#include <QDateTime>

#include "network/exifdate.h"

namespace {

// Builds a JPEG-shaped byte string: SOI, then the given segments.
QByteArray jpeg(const QByteArray& segments = QByteArray())
{
    QByteArray out;
    out.append('\xFF');
    out.append('\xD8');  // SOI
    out.append(segments);
    return out;
}

// An APP1 segment carrying `payload`. The two length bytes cover the length
// field itself plus the payload, which is why the +2 is here and the reader
// subtracts it back off.
QByteArray app1(const QByteArray& payload)
{
    const int length = static_cast<int>(payload.size()) + 2;
    QByteArray seg;
    seg.append('\xFF');
    seg.append('\xE1');
    seg.append(static_cast<char>((length >> 8) & 0xFF));
    seg.append(static_cast<char>(length & 0xFF));
    seg.append(payload);
    return seg;
}

// A well-formed Exif payload whose DateTimeOriginal reads `date`.
QByteArray exifPayload(const QByteArray& date)
{
    QByteArray p = QByteArrayLiteral("Exif\0\0");
    p.append(QByteArrayLiteral("II*\0\x08\0\0\0"));  // TIFF header, plausible filler
    p.append(date);
    return p;
}

// An arbitrary non-EXIF APP segment, used to check the reader walks past it.
QByteArray appN(char marker, const QByteArray& payload)
{
    const int length = static_cast<int>(payload.size()) + 2;
    QByteArray seg;
    seg.append('\xFF');
    seg.append(marker);
    seg.append(static_cast<char>((length >> 8) & 0xFF));
    seg.append(static_cast<char>(length & 0xFF));
    seg.append(payload);
    return seg;
}

}  // namespace

class TestExifDate : public QObject
{
    Q_OBJECT

private slots:
    // The case the dead-code bug broke: a real EXIF date must come back.
    void readsDateTimeOriginal()
    {
        const QDateTime dt = ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("2026:03:21 11:20:41"))));

        QVERIFY2(dt.isValid(), "A well-formed EXIF date must parse");
        QCOMPARE(dt.date(), QDate(2026, 3, 21));
        QCOMPARE(dt.time(), QTime(11, 20, 41));
    }

    // The reader must skip other APP markers rather than give up at the first
    // one — cameras routinely emit APP0/JFIF before APP1/Exif.
    void skipsPrecedingAppSegments()
    {
        QByteArray segments;
        segments.append(appN('\xE0', QByteArrayLiteral("JFIF\0\x01\x02")));  // APP0
        segments.append(appN('\xED', QByteArrayLiteral("Photoshop 3.0\0")));  // APP13
        segments.append(app1(exifPayload("2019:12:25 08:00:00")));

        const QDateTime dt = ExifDate::fromJpegBytes(jpeg(segments));
        QVERIFY2(dt.isValid(), "EXIF after other APP segments must still be found");
        QCOMPARE(dt.date(), QDate(2019, 12, 25));
    }

    void rejectsNonJpeg()
    {
        QVERIFY(!ExifDate::fromJpegBytes(QByteArrayLiteral("PK\x03\x04 not a jpeg")).isValid());
        QVERIFY(!ExifDate::fromJpegBytes(QByteArray()).isValid());
        QVERIFY(!ExifDate::fromJpegBytes(QByteArrayLiteral("\xFF")).isValid());
    }

    // A JPEG with no EXIF is the ordinary case for a screenshot or a re-encode,
    // not an error — it must return invalid rather than inventing a date.
    void jpegWithoutExifYieldsInvalid()
    {
        QVERIFY(!ExifDate::fromJpegBytes(jpeg()).isValid());
        QVERIFY(!ExifDate::fromJpegBytes(
            jpeg(appN('\xE0', QByteArrayLiteral("JFIF\0")))).isValid());
    }

    // An APP1 that is not Exif (XMP uses APP1 too) must not be scanned for a
    // date, even when its payload happens to contain a date-shaped string.
    void app1WithoutExifHeaderIsIgnored()
    {
        const QByteArray xmp =
            QByteArrayLiteral("http://ns.adobe.com/xap/1.0/\0 2020:01:01 00:00:00");
        QVERIFY(!ExifDate::fromJpegBytes(jpeg(app1(xmp))).isValid());
    }

    // The year guard exists because the regex matches any date-shaped bytes,
    // and EXIF blobs are full of binary that can look numeric.
    void rejectsImplausibleYears()
    {
        QVERIFY(!ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("1889:03:21 11:20:41")))).isValid());
        QVERIFY(!ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("2999:03:21 11:20:41")))).isValid());
    }

    // Boundary years must be accepted; the guard is inclusive by intent.
    void acceptsBoundaryYears()
    {
        QVERIFY(ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("1990:01:01 00:00:00")))).isValid());
        QVERIFY(ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("2100:12:31 23:59:59")))).isValid());
    }

    // A date-shaped string that is not a real date must not produce a valid
    // QDateTime — month 13 and day 32 both have to fail.
    void rejectsImpossibleDates()
    {
        QVERIFY(!ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("2026:13:01 10:00:00")))).isValid());
        QVERIFY(!ExifDate::fromJpegBytes(
            jpeg(app1(exifPayload("2026:02:32 10:00:00")))).isValid());
    }

    // The caller reads only the first 64 KB, so a segment header can promise
    // more bytes than are present. That must degrade to "no date", never crash
    // or read out of bounds — this test is meaningful under ASan, which is on
    // for every Debug build.
    void truncatedSegmentDoesNotOverrun()
    {
        QByteArray seg = app1(exifPayload("2026:03:21 11:20:41"));
        for (int cut = 1; cut < seg.size(); ++cut) {
            const QByteArray truncated = jpeg(seg.left(seg.size() - cut));
            (void)ExifDate::fromJpegBytes(truncated);  // must not crash
        }
        QVERIFY(true);
    }

    // A length field claiming far more than the buffer holds is the same
    // hazard reached a different way: mid() clamps, and the scan must end.
    void oversizedLengthFieldIsSurvivable()
    {
        QByteArray seg;
        seg.append('\xFF');
        seg.append('\xE1');
        seg.append('\xFF');  // length = 65535, far beyond what follows
        seg.append('\xFF');
        seg.append(QByteArrayLiteral("Exif\0\0"));
        (void)ExifDate::fromJpegBytes(jpeg(seg));
        QVERIFY(true);
    }

    // Bytes that are not markers must be walked over, not treated as segment
    // headers — entropy-coded scan data is full of them.
    void nonMarkerBytesAreSkipped()
    {
        QByteArray segments;
        segments.append(QByteArrayLiteral("\x12\x34\x56\x78\x00\x00"));
        segments.append(app1(exifPayload("2021:06:15 14:30:00")));

        const QDateTime dt = ExifDate::fromJpegBytes(jpeg(segments));
        QVERIFY(dt.isValid());
        QCOMPARE(dt.date(), QDate(2021, 6, 15));
    }
};

QTEST_MAIN(TestExifDate)
#include "tst_exifdate.moc"
