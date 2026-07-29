#include <QtTest>

#include "network/wifiscaleresult.h"

using namespace WifiScaleResultUtil;

// Pure discovery-result logic: TXT parsing, address dedupe, row labelling.
//
// These are deliberately free functions in their own TU because the mDNS
// transport that feeds them needs real packets on a real LAN. Everything decidable from data
// rather than packets lives here so it IS covered on every platform.
//
// The TXT fixtures are the two records actually captured from live scales on
// 2026-07-28, not invented ones. Both quirks they encode (a missing "name" key,
// and "fw" carrying a "FW: " prefix) were surprises that contradicted the
// firmware source, so they are the cases most worth pinning down.
class tst_WifiScaleResult : public QObject {
    Q_OBJECT

    // "Half Decent Scale" @ hds.local — firmware 3.1.12. Note: NO "name" key.
    static QMap<QString, QString> txtUnrenamed() {
        return {{"path", "/snapshot"}, {"proto", "ws"},
                {"model", "hds"}, {"fw", "FW: 3.1.12"}};
    }

    // "Half Decent Scale (hdstest)" @ hdstest.local — firmware 3.1.13-dev.
    static QMap<QString, QString> txtRenamed() {
        return {{"path", "/snapshot"}, {"proto", "ws"}, {"name", "hdstest"},
                {"model", "hds"}, {"fw", "FW: 3.1.13-dev"}};
    }

    static WifiScaleResult fallbackAt(const QString& hostname, const QString& address) {
        WifiScaleResult r;
        r.foundBy = WifiScaleResult::Source::Fallback;
        r.hostname = hostname;
        r.address = address;
        return r;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ===== TXT parsing (task 8.2) =====

    void parsesRealUnrenamedRecord() {
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("192.168.10.145"), 80, txtUnrenamed());

        QCOMPARE(r.instanceName, QStringLiteral("Half Decent Scale"));
        QCOMPARE(r.hostname, QStringLiteral("hds.local"));
        QCOMPARE(r.address, QStringLiteral("192.168.10.145"));
        QCOMPARE(r.port, quint16(80));
        QCOMPARE(r.path, QStringLiteral("/snapshot"));
        QCOMPARE(r.firmwareVersion, QStringLiteral("3.1.12"));
        // The key is genuinely absent on this firmware — must not fabricate one.
        QVERIFY(r.mdnsName.isEmpty());
        QCOMPARE(r.foundBy, WifiScaleResult::Source::Browse);
    }

    void parsesRealRenamedRecord() {
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("Half Decent Scale (hdstest)"), QStringLiteral("hdstest.local"),
            QStringLiteral("192.168.10.241"), 80, txtRenamed());

        QCOMPARE(r.mdnsName, QStringLiteral("hdstest"));
        QCOMPARE(r.firmwareVersion, QStringLiteral("3.1.13-dev"));
        QCOMPARE(r.path, QStringLiteral("/snapshot"));
    }

    void emptyTxtFallsBackToDefaults() {
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("Some Scale"), QStringLiteral("x.local"),
            QStringLiteral("10.0.0.5"), 80, {});

        // Missing keys mean "use the default", never "reject the record".
        QCOMPARE(r.path, QStringLiteral("/snapshot"));
        QVERIFY(r.mdnsName.isEmpty());
        QVERIFY(r.firmwareVersion.isEmpty());
    }

    void unknownTxtKeysAreIgnored() {
        QMap<QString, QString> txt = txtRenamed();
        txt.insert(QStringLiteral("future_field"), QStringLiteral("whatever"));
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("S"), QStringLiteral("x.local"),
            QStringLiteral("10.0.0.5"), 80, txt);
        QCOMPARE(r.mdnsName, QStringLiteral("hdstest"));
    }

    void pathGainsLeadingSlash() {
        QMap<QString, QString> txt{{"path", "snapshot"}};
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("S"), QStringLiteral("x.local"),
            QStringLiteral("10.0.0.5"), 80, txt);
        QCOMPARE(r.path, QStringLiteral("/snapshot"));
    }

    void zeroSrvPortFallsBackToEighty() {
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("S"), QStringLiteral("x.local"),
            QStringLiteral("10.0.0.5"), 0, {});
        QCOMPARE(r.port, quint16(80));
    }

    void firmwareVersionNormalization_data() {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("expected");

        QTest::newRow("real 3.1.12")   << "FW: 3.1.12"     << "3.1.12";
        QTest::newRow("real dev")      << "FW: 3.1.13-dev" << "3.1.13-dev";
        QTest::newRow("no space")      << "FW:3.1.12"      << "3.1.12";
        QTest::newRow("lowercase")     << "fw: 3.1.12"     << "3.1.12";
        QTest::newRow("bare version")  << "3.1.12"         << "3.1.12";
        QTest::newRow("empty")         << ""               << "";
        // Unparseable is kept verbatim rather than dropped — showing the user
        // something true beats showing them nothing.
        QTest::newRow("garbage kept")  << "not-a-version"  << "not-a-version";
    }

    void firmwareVersionNormalization() {
        QFETCH(QString, raw);
        QFETCH(QString, expected);
        QCOMPARE(normalizeFirmwareVersion(raw), expected);
    }

    // ===== Upsert — the shape results actually arrive in (task 8.1) =====
    //
    // One at a time, from two concurrent discovery paths. An earlier version of
    // these tests exercised a batch merge function that production never called,
    // so they passed while the shipping dedupe did something different.

    static QVector<WifiScaleResult> setWith(std::initializer_list<WifiScaleResult> rs) {
        QVector<WifiScaleResult> out;
        for (const auto& r : rs) upsertByHostname(out, r);
        return out;
    }

    void browseSupersedesFallbackAtSameAddress() {
        QVector<WifiScaleResult> set;
        QVERIFY(upsertByHostname(set, fallbackAt(QStringLiteral("hds.local"),
                                                QStringLiteral("192.168.10.145"))));
        QVERIFY(upsertByHostname(set, fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("192.168.10.145"), 80, txtUnrenamed())));

        QCOMPARE(set.size(), 1);
        QCOMPARE(set.first().foundBy, WifiScaleResult::Source::Browse);
        QCOMPARE(set.first().firmwareVersion, QStringLiteral("3.1.12"));
    }

    void fallbackNeverDowngradesABrowseEntry() {
        QVector<WifiScaleResult> set;
        upsertByHostname(set, fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("192.168.10.145"), 80, txtUnrenamed()));
        upsertByHostname(set, fallbackAt(QStringLiteral("hds.local"),
                                        QStringLiteral("192.168.10.145")));

        QCOMPARE(set.size(), 1);
        QCOMPARE(set.first().foundBy, WifiScaleResult::Source::Browse);
        QCOMPARE(set.first().firmwareVersion, QStringLiteral("3.1.12"));
    }

    void differentAddressesBothSurvive() {
        const QVector<WifiScaleResult> set = setWith({
            fromBrowseTxt(QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
                          QStringLiteral("192.168.10.145"), 80, txtUnrenamed()),
            fallbackAt(QStringLiteral("hds-2.local"), QStringLiteral("192.168.10.241")),
        });
        QCOMPARE(set.size(), 2);
    }

    void dhcpAddressChangeUpdatesInPlaceRatherThanAddingARow() {
        // The reason identity is the hostname: a lease change must not strand
        // the old entry and create a second one for the same scale.
        QVector<WifiScaleResult> set;
        upsertByHostname(set, fallbackAt(QStringLiteral("hds.local"), QStringLiteral("10.0.0.1")));
        QVERIFY(upsertByHostname(set, fallbackAt(QStringLiteral("hds.local"), QStringLiteral("10.0.0.9"))));
        QCOMPARE(set.size(), 1);
        QCOMPARE(set.first().address, QStringLiteral("10.0.0.9"));
    }

    void hostnameKeyIgnoresCaseAndTrailingDot() {
        QVector<WifiScaleResult> set;
        upsertByHostname(set, fallbackAt(QStringLiteral("hds.local"), QStringLiteral("10.0.0.1")));
        upsertByHostname(set, fallbackAt(QStringLiteral("HDS.local."), QStringLiteral("10.0.0.1")));
        QCOMPARE(set.size(), 1);
    }

    void addresslessResultsAreRejected() {
        // An unresolved hit has no address, so it can neither be deduped nor
        // connected to. It must never enter the set.
        QVector<WifiScaleResult> set;
        QVERIFY(!upsertByHostname(set, fallbackAt(QStringLiteral("ghost.local"), QString())));
        QVERIFY(set.isEmpty());
    }

    void repeatedIdenticalResultDoesNotGrowTheSet() {
        const WifiScaleResult a = fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("192.168.10.145"), 80, txtUnrenamed());
        QVector<WifiScaleResult> set;
        QVERIFY(upsertByHostname(set, a));
        // The return value is the contract — "true if the set changed" — and
        // mDNS re-announces the same record throughout a browse, so this is the
        // common case, not an edge one. Asserting only set.size() let the browse
        // branch return true unconditionally.
        QVERIFY(!upsertByHostname(set, a));
        QCOMPARE(set.size(), 1);

        // A browse hit that genuinely differs still reports a change.
        WifiScaleResult moved = a;
        moved.address = QStringLiteral("192.168.10.200");
        QVERIFY(upsertByHostname(set, moved));
        QCOMPARE(set.size(), 1);
        QCOMPARE(set.first().address, QStringLiteral("192.168.10.200"));
    }

    // ===== Set-wide labelling =====

    void twoUnrenamedScalesBothGetAddresses() {
        const QVector<WifiScaleResult> set = setWith({
            fromBrowseTxt(QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
                          QStringLiteral("192.168.10.145"), 80, txtUnrenamed()),
            fromBrowseTxt(QStringLiteral("Half Decent Scale-2"), QStringLiteral("other.local"),
                          QStringLiteral("192.168.10.241"), 80, txtUnrenamed()),
        });
        const QHash<QString, QString> labels = labelsByHostname(set);
        QCOMPARE(labels.value(QStringLiteral("hds.local")),
                 QStringLiteral("Half Decent Scale (192.168.10.145)"));
        QCOMPARE(labels.value(QStringLiteral("other.local")),
                 QStringLiteral("Half Decent Scale-2 (192.168.10.241)"));
    }

    void distinctlyNamedScalesKeepPlainLabels() {
        const QVector<WifiScaleResult> set = setWith({
            fromBrowseTxt(QStringLiteral("Half Decent Scale (kitchen)"), QStringLiteral("kitchen.local"),
                          QStringLiteral("10.0.0.1"), 80, {}),
            fromBrowseTxt(QStringLiteral("Half Decent Scale (hdstest)"), QStringLiteral("hdstest.local"),
                          QStringLiteral("10.0.0.2"), 80, {}),
        });
        const QHash<QString, QString> labels = labelsByHostname(set);
        QCOMPARE(labels.value(QStringLiteral("kitchen.local")), QStringLiteral("Half Decent Scale (kitchen)"));
        QCOMPARE(labels.value(QStringLiteral("hdstest.local")), QStringLiteral("Half Decent Scale (hdstest)"));
    }

    // The fallback list this project ships probes hds/hds-2/hds-3. Those are
    // DIFFERENT user-chosen names, not a DNS-SD collision suffix, so they must
    // not be collapsed — otherwise every multi-scale fallback discovery is
    // flagged ambiguous and every row shows a bare IP.
    void fallbackHostnamesWithDigitsAreNotTreatedAsCollisions() {
        const QVector<WifiScaleResult> set = setWith({
            fallbackAt(QStringLiteral("hds.local"), QStringLiteral("10.0.0.1")),
            fallbackAt(QStringLiteral("hds-2.local"), QStringLiteral("10.0.0.2")),
        });
        const QHash<QString, QString> labels = labelsByHostname(set);
        QCOMPARE(labels.value(QStringLiteral("hds.local")), QStringLiteral("hds"));
        QCOMPARE(labels.value(QStringLiteral("hds-2.local")), QStringLiteral("hds-2"));
    }

    // ===== Display names (task 8.3) =====

    void displayNameUsesInstanceNameWhenUnambiguous() {
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("Half Decent Scale (hdstest)"), QStringLiteral("hdstest.local"),
            QStringLiteral("192.168.10.241"), 80, txtRenamed());
        QCOMPARE(displayName(r, false), QStringLiteral("Half Decent Scale (hdstest)"));
    }

    void displayNameForFallbackOnlyHitDerivesFromHostname() {
        const WifiScaleResult r =
            fallbackAt(QStringLiteral("hds-2.local"), QStringLiteral("10.0.0.9"));
        QCOMPARE(displayName(r, false), QStringLiteral("hds-2"));
    }

    void displayNameAppendsAddressWhenAmbiguous() {
        const WifiScaleResult r = fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("192.168.10.145"), 80, txtUnrenamed());
        QCOMPARE(displayName(r, true),
                 QStringLiteral("Half Decent Scale (192.168.10.145)"));
    }

    void dnsSdCollisionSuffixCountsAsAmbiguous() {
        // Two unrenamed scales: DNS-SD suffixes one to "-2". The user cannot
        // tell these apart from the labels alone, so they must disambiguate.
        const WifiScaleResult a = fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("10.0.0.1"), 80, {});
        const WifiScaleResult b = fromBrowseTxt(
            QStringLiteral("Half Decent Scale-2"), QStringLiteral("other.local"),
            QStringLiteral("10.0.0.2"), 80, {});
        QVERIFY(labelsCollide(a, b));
    }

    void genuinelyDifferentNamesDoNotCollide() {
        const WifiScaleResult a = fromBrowseTxt(
            QStringLiteral("Half Decent Scale (kitchen)"), QStringLiteral("kitchen.local"),
            QStringLiteral("10.0.0.1"), 80, {});
        const WifiScaleResult b = fromBrowseTxt(
            QStringLiteral("Half Decent Scale (hdstest)"), QStringLiteral("hdstest.local"),
            QStringLiteral("10.0.0.2"), 80, {});
        QVERIFY(!labelsCollide(a, b));
    }

    // The pair BLEManager actually has to disambiguate: two unrenamed scales,
    // one suffixed by DNS-SD. Both rows must end up showing their address,
    // because neither label identifies a physical scale on its own.
    void collidingPairBothGetAddresses() {
        const WifiScaleResult a = fromBrowseTxt(
            QStringLiteral("Half Decent Scale"), QStringLiteral("hds.local"),
            QStringLiteral("192.168.10.145"), 80, txtUnrenamed());
        const WifiScaleResult b = fromBrowseTxt(
            QStringLiteral("Half Decent Scale-2"), QStringLiteral("other.local"),
            QStringLiteral("192.168.10.241"), 80, txtUnrenamed());

        QVERIFY(labelsCollide(a, b));
        QCOMPARE(displayName(a, true), QStringLiteral("Half Decent Scale (192.168.10.145)"));
        QCOMPARE(displayName(b, true), QStringLiteral("Half Decent Scale-2 (192.168.10.241)"));
    }

    void userChosenTrailingDigitsSurviveCollisionStripping() {
        // "kitchen-2" is a legal user-chosen name. It arrives inside the
        // instance label, and must not be mistaken for a DNS-SD "-2" suffix and
        // collapsed against an unrelated "kitchen".
        const WifiScaleResult a = fromBrowseTxt(
            QStringLiteral("Half Decent Scale (kitchen-2)"), QStringLiteral("kitchen-2.local"),
            QStringLiteral("10.0.0.1"), 80, {});
        const WifiScaleResult b = fromBrowseTxt(
            QStringLiteral("Half Decent Scale (kitchen)"), QStringLiteral("kitchen.local"),
            QStringLiteral("10.0.0.2"), 80, {});
        QVERIFY(!labelsCollide(a, b));
    }
};

QTEST_GUILESS_MAIN(tst_WifiScaleResult)
#include "tst_wifiscaleresult.moc"
