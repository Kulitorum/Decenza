#include <QtTest>

#include "core/hdsfirmwarecatalog.h"

class tst_HdsFirmwareCatalog : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void selectsNewestCompatibleRelease();
    void ignoresCurrentAndOlderRelease();
    void rejectsDifferentModel();
    void rejectsReleaseBlockedByMinFrom();
    void offersEqualNumberedStableToAPreviewOrRcInstall();
    void rejectsNonStableVersion();
    void rejectsMalformedOrEmptyCatalog();
    void storesEveryVersionAsAWireReadyTarget();
};

void tst_HdsFirmwareCatalog::selectsNewestCompatibleRelease()
{
    const auto catalog = HdsFirmwareCatalog::fromJson(R"({
        "model":"hds", "version":"3.1.13", "min_from":"3.0.0",
        "release_notes_url":"https://example.test/releases/v3.1.13",
        "releases":[
            {"model":"hds","version":"3.1.13","min_from":"3.0.0",
             "release_notes_url":"https://example.test/releases/v3.1.13"},
            {"model":"hds","version":"3.1.12","min_from":"3.0.0"}
        ]
    })");
    QVERIFY(catalog);

    const auto release = catalog->newestEligibleRelease(QStringLiteral("3.1.10"));
    QVERIFY(release);
    QCOMPARE(release->version, QStringLiteral("3.1.13"));
    QCOMPARE(release->releaseNotesUrl, QStringLiteral("https://example.test/releases/v3.1.13"));
}

void tst_HdsFirmwareCatalog::ignoresCurrentAndOlderRelease()
{
    const auto catalog = HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.13","releases":[
            {"model":"hds","version":"3.1.13"},
            {"model":"hds","version":"3.1.12"}
        ]})");
    QVERIFY(catalog);
    QVERIFY(!catalog->newestEligibleRelease(QStringLiteral("3.1.13")));
}

void tst_HdsFirmwareCatalog::rejectsDifferentModel()
{
    const auto catalog = HdsFirmwareCatalog::fromJson(
        R"({"model":"other-scale","version":"3.1.13"})");
    QVERIFY(catalog);
    QVERIFY(!catalog->newestEligibleRelease(QStringLiteral("3.1.10")));
}

void tst_HdsFirmwareCatalog::rejectsReleaseBlockedByMinFrom()
{
    const auto catalog = HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.13","min_from":"3.1.0"})");
    QVERIFY(catalog);
    QVERIFY(!catalog->newestEligibleRelease(QStringLiteral("3.0.9")));
}

// Mirrors pull_ota.h's pullOtaBuildSelectableReleases(): a scale running a
// preview/rc build is offered the equal-numbered stable release too — never a
// strictly older one, and never for an arbitrary non-preview/rc suffix.
void tst_HdsFirmwareCatalog::offersEqualNumberedStableToAPreviewOrRcInstall()
{
    const auto catalog = HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14"})");
    QVERIFY(catalog);

    const auto preview = catalog->newestEligibleRelease(QStringLiteral("3.1.14-preview.4"));
    QVERIFY(preview);
    QCOMPARE(preview->version, QStringLiteral("3.1.14"));

    const auto rc = catalog->newestEligibleRelease(QStringLiteral("3.1.14-rc.2"));
    QVERIFY(rc);
    QCOMPARE(rc->version, QStringLiteral("3.1.14"));

    // An arbitrary suffix isn't a recognized preview/rc build, so it gets no
    // exception — same as a stable install already at the newest version.
    QVERIFY(!catalog->newestEligibleRelease(QStringLiteral("3.1.14-dev")));

    // The exception is "equal", never "older": a preview build ahead of the
    // catalog's newest stable release must not be offered a downgrade.
    QVERIFY(!catalog->newestEligibleRelease(QStringLiteral("3.1.15-preview.1")));
}

void tst_HdsFirmwareCatalog::rejectsNonStableVersion()
{
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14-dev"})"));
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14+metadata"})"));
    // Out-of-range is one branch, covered by storesEveryVersionAsAWireReadyTarget.
}

// The catalog is where a version becomes safe to put on the wire, so that no
// transport has to decide for itself and they cannot drift apart. Two rules:
// a leading `v` is normalised away, and a component the transport cannot carry
// is refused rather than clamped into a different, installable release.
void tst_HdsFirmwareCatalog::storesEveryVersionAsAWireReadyTarget()
{
    const auto catalog = HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"v3.1.14","min_from":"v3.0.0","pcb":"1.3"})");
    QVERIFY(catalog);
    QCOMPARE(catalog->releases().first().version, QStringLiteral("3.1.14"));
    QCOMPARE(catalog->releases().first().minFromVersion, QStringLiteral("3.0.0"));
    QCOMPARE(catalog->releases().first().pcb, QStringLiteral("1.3"));

    // Still eligible against an installed version reported the other way.
    const auto release = catalog->newestEligibleRelease(QStringLiteral("3.1.13"));
    QVERIFY(release);
    QCOMPARE(release->version, QStringLiteral("3.1.14"));

    // 127 is the largest component a payload byte can carry.
    QVERIFY(HdsFirmwareCatalog::fromJson(R"({"model":"hds","version":"3.1.127"})"));
    QVERIFY(!HdsFirmwareCatalog::fromJson(R"({"model":"hds","version":"3.1.128"})"));

    // A prerelease is ORDERED but never OFFERED. The installed version may be one
    // — a scale running 3.1.14-preview.1 must compare as 3.1.14, which is what
    // lets the WiFi driver report the version exactly as discovery shows it —
    // while a catalog entry carrying a suffix is not a published release.
    QCOMPARE(HdsFirmwareCatalog::compareVersions(QStringLiteral("3.1.14-preview.1"),
                                                 QStringLiteral("3.1.14")), 0);
    QVERIFY(!HdsFirmwareCatalog::fromJson(R"({"model":"hds","version":"3.1.14-preview.1"})"));

    const auto catalogFromPreview = HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.15"})");
    QVERIFY(catalogFromPreview);
    QVERIFY(catalogFromPreview->newestEligibleRelease(QStringLiteral("3.1.14-preview.1")));
}

void tst_HdsFirmwareCatalog::rejectsMalformedOrEmptyCatalog()
{
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14","releases":{}})"));
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14","releases":[]})"));
}

QTEST_GUILESS_MAIN(tst_HdsFirmwareCatalog)
#include "tst_hdsfirmwarecatalog.moc"
