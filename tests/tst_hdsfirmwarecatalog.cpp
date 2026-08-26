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
    void rejectsNonStableVersion();
    void rejectsMalformedOrEmptyCatalog();
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

void tst_HdsFirmwareCatalog::rejectsNonStableVersion()
{
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14-dev"})"));
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"3.1.14+metadata"})"));
    QVERIFY(!HdsFirmwareCatalog::fromJson(
        R"({"model":"hds","version":"65536.1.1"})"));
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
