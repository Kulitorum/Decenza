#include <QtTest>

#include "../src/network/screensavermediarange.h"

class tst_ScreensaverMediaRange : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void fullAndExplicitRanges();
    void suffixAndClamp();
    void rejectsMalformedAndMultipleRanges();
    void mediaPumpRespectsHighWaterBound();
};

void tst_ScreensaverMediaRange::fullAndExplicitRanges()
{
    ScreensaverMediaRange::Range range;
    QVERIFY(ScreensaverMediaRange::parse({}, 100, &range));
    QCOMPARE(range.first, 0);
    QCOMPARE(range.last, 99);
    QCOMPARE(range.length(), 100);
    QVERIFY(!range.partial);

    QVERIFY(ScreensaverMediaRange::parse("bytes=10-19", 100, &range));
    QCOMPARE(range.first, 10);
    QCOMPARE(range.last, 19);
    QCOMPARE(range.length(), 10);
    QVERIFY(range.partial);

    QVERIFY(ScreensaverMediaRange::parse("bytes=20-", 100, &range));
    QCOMPARE(range.first, 20);
    QCOMPARE(range.last, 99);
}

void tst_ScreensaverMediaRange::suffixAndClamp()
{
    ScreensaverMediaRange::Range range;
    QVERIFY(ScreensaverMediaRange::parse("bytes=-8", 100, &range));
    QCOMPARE(range.first, 92);
    QCOMPARE(range.last, 99);
    QVERIFY(ScreensaverMediaRange::parse("bytes=-200", 100, &range));
    QCOMPARE(range.first, 0);
    QCOMPARE(range.last, 99);
    QVERIFY(ScreensaverMediaRange::parse("bytes=98-900", 100, &range));
    QCOMPARE(range.first, 98);
    QCOMPARE(range.last, 99);
}

void tst_ScreensaverMediaRange::rejectsMalformedAndMultipleRanges()
{
    ScreensaverMediaRange::Range range;
    for (const QByteArray& header : {
             QByteArrayLiteral("items=0-1"), QByteArrayLiteral("bytes=100-"),
             QByteArrayLiteral("bytes=8-4"), QByteArrayLiteral("bytes=-0"),
             QByteArrayLiteral("bytes=+1-2"), QByteArrayLiteral("bytes=1-2,4-5"),
             QByteArrayLiteral("bytes=1--2"), QByteArrayLiteral("bytes=9223372036854775808-")}) {
        QVERIFY2(!ScreensaverMediaRange::parse(header, 100, &range), header.constData());
    }
    QVERIFY(!ScreensaverMediaRange::parse({}, 0, &range));
    QVERIFY(!ScreensaverMediaRange::parse("bytes=0-0", 100, nullptr));
}

void tst_ScreensaverMediaRange::mediaPumpRespectsHighWaterBound()
{
    using namespace ScreensaverMediaRange;
    QCOMPARE(nextChunkSize(0, 2 * mediaChunkBytes), mediaChunkBytes);
    QCOMPARE(nextChunkSize(socketHighWaterBytes - mediaChunkBytes,
                           2 * mediaChunkBytes), mediaChunkBytes);
    QCOMPARE(nextChunkSize(socketHighWaterBytes - mediaChunkBytes / 2,
                           2 * mediaChunkBytes), mediaChunkBytes / 2);
    QCOMPARE(nextChunkSize(socketHighWaterBytes - 1, mediaChunkBytes), 1);
    QCOMPARE(nextChunkSize(0, 10), 10);
    QCOMPARE(nextChunkSize(socketHighWaterBytes, mediaChunkBytes), 0);
    QCOMPARE(nextChunkSize(-1, mediaChunkBytes), 0);
    QCOMPARE(nextChunkSize(0, 0), 0);
}

QTEST_GUILESS_MAIN(tst_ScreensaverMediaRange)
#include "tst_screensavermediarange.moc"
