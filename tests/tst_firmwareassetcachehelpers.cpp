#include <QtTest>

#include "core/firmwareassetcache.h"

// Pure helpers used by FirmwareAssetCache:
//   - MetaJson round-trip (sidecar .meta.json that tracks {etag, version,
//     downloadedAtEpoch} so we don't re-download unchanged firmware)
//   - rangeHeaderFor() — computes the correct HTTP Range header (or
//     nothing) given an existing partial-file size
//
// The FirmwareAssetCache class itself talks to QNetworkAccessManager and
// is covered by the §5 integration test, not here.

class tst_FirmwareAssetCacheHelpers : public QObject {
    Q_OBJECT

private slots:

    // ===== MetaJson round-trip =====

    void metaJson_roundTripsExactly() {
        DE1::Firmware::MetaJson in;
        in.etag              = QStringLiteral("\"bcfebcf3449b8933288a51fa79b0751c\"");
        in.version           = 1352;
        in.downloadedAtEpoch = 1776696227;

        QByteArray json = DE1::Firmware::serializeMeta(in);
        QVERIFY(!json.isEmpty());

        auto parsed = DE1::Firmware::parseMeta(json);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->etag,              in.etag);
        QCOMPARE(parsed->version,           in.version);
        QCOMPARE(parsed->downloadedAtEpoch, in.downloadedAtEpoch);
    }

    void metaJson_parsesEmptyEtag() {
        // An empty etag is legal when the sidecar is being written before
        // a real server response is known. Must not reject.
        DE1::Firmware::MetaJson in;
        in.etag = QString();
        in.version = 0;
        in.downloadedAtEpoch = 0;

        QByteArray json = DE1::Firmware::serializeMeta(in);
        auto parsed = DE1::Firmware::parseMeta(json);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->etag.isEmpty(), true);
    }

    void metaJson_rejectsMalformed() {
        QCOMPARE(DE1::Firmware::parseMeta(QByteArray()).has_value(), false);
        QCOMPARE(DE1::Firmware::parseMeta(QByteArray("{")).has_value(), false);
        QCOMPARE(DE1::Firmware::parseMeta(QByteArray("not json")).has_value(), false);
    }

    void metaJson_rejectsWrongTypes() {
        // "version" must be a number, not a string. Reject so we don't
        // silently accept a broken sidecar and mis-compare versions.
        QByteArray bad = QByteArray(
            R"({"etag":"abc","version":"1352","downloadedAtEpoch":0})");
        QCOMPARE(DE1::Firmware::parseMeta(bad).has_value(), false);
    }

    void metaJson_ignoresExtraFields() {
        // Forward-compat: if a future Decenza adds new fields to the
        // sidecar, an older version must still parse the fields it knows.
        QByteArray withExtras = QByteArray(
            R"({"etag":"xyz","version":1352,"downloadedAtEpoch":100,"futureField":"ignore me"})");
        auto parsed = DE1::Firmware::parseMeta(withExtras);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->etag,    QStringLiteral("xyz"));
        QCOMPARE(parsed->version, uint32_t(1352));
    }

    // ===== rangeHeaderFor: compute resume Range header =====

    void rangeHeaderFor_noPartial_returnsEmpty() {
        // Nothing on disk yet → no Range header (do a fresh GET).
        auto r = DE1::Firmware::rangeHeaderFor(/*existingSize*/ 0,
                                               /*expectedTotal*/ 453000);
        QCOMPARE(r.has_value(), false);
    }

    void rangeHeaderFor_partial_returnsResumeHeader() {
        // 100 000 bytes on disk out of 453 000 → Range: bytes=100000-
        auto r = DE1::Firmware::rangeHeaderFor(100000, 453000);
        QVERIFY(r.has_value());
        QCOMPARE(*r, QByteArray("bytes=100000-"));
    }

    void rangeHeaderFor_completeFile_returnsEmpty() {
        // If we already have the whole file, there's nothing to resume —
        // caller should skip the download entirely, not ask for a Range.
        auto r = DE1::Firmware::rangeHeaderFor(453000, 453000);
        QCOMPARE(r.has_value(), false);
    }

    void rangeHeaderFor_overrun_returnsEmpty() {
        // existingSize > expectedTotal would mean our cache is bigger than
        // the server claims — the cache is stale/corrupt; caller must
        // wipe and re-download rather than sending a bogus Range.
        auto r = DE1::Firmware::rangeHeaderFor(1000000, 453000);
        QCOMPARE(r.has_value(), false);
    }

    void rangeHeaderFor_unknownTotal_partialOnly() {
        // If the server didn't report a Content-Length yet, we still want
        // to resume from what we have on disk.
        auto r = DE1::Firmware::rangeHeaderFor(50000, /*unknown*/ -1);
        QVERIFY(r.has_value());
        QCOMPARE(*r, QByteArray("bytes=50000-"));
    }
};

QTEST_GUILESS_MAIN(tst_FirmwareAssetCacheHelpers)
#include "tst_firmwareassetcachehelpers.moc"
