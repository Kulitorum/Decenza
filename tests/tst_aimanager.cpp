// tst_aimanager — pins the canonical-source separation contract from
// openspec optimize-dialing-context-payload, task 10.5.
//
// Specifically: when AIManager renders a multi-shot history block via
// requestRecentShotContext (the in-app "Previous Shots with This Bean &
// Profile" path), profile metadata + setup identity must be hoisted to
// a single header at the top of the section. Per-shot blocks render in
// HistoryBlock mode and must NOT carry repeated profile intent or
// grinder/bean identity strings.
//
// The test exercises emitRecentShotContext directly via the
// `friend class tst_AIManager` pattern so it can synthesize a 4-shot
// `qualifiedShots` list inline — no real DB stand-up needed. The
// resulting payload is captured by QSignalSpy on recentShotContextReady
// and asserted for exactly-once occurrences of the hoisted strings.

#include <QtTest>
#include <QSignalSpy>
#include <QNetworkAccessManager>
#include <QPair>
#include <QList>
#include <QString>
#include <QStandardPaths>
#include <QDir>

#include "ai/aimanager.h"
#include "core/settings.h"
#include "history/shotprojection.h"
#include "history/shothistory_types.h"

namespace {

// Build a minimal but complete ShotProjection that summarizeFromHistory
// will accept (non-zero dose / yield / duration so the block renders).
ShotProjection makeShot(qint64 id, qint64 timestamp,
                        const QString& grinderBrand,
                        const QString& grinderModel,
                        const QString& grinderBurrs,
                        const QString& grinderSetting,
                        const QString& beanBrand,
                        const QString& beanType,
                        const QString& profileName,
                        const QString& profileNotes,
                        const QString& profileJson)
{
    ShotProjection p;
    p.id = id;
    p.timestamp = timestamp;
    p.timestampIso = QDateTime::fromSecsSinceEpoch(timestamp).toString(Qt::ISODate);
    p.profileName = profileName;
    p.profileNotes = profileNotes;
    p.profileJson = profileJson;
    p.beverageType = QStringLiteral("espresso");
    p.doseWeightG = 18.0;
    p.finalWeightG = 36.0;
    p.durationSec = 30.0;
    p.grinderBrand = grinderBrand;
    p.grinderModel = grinderModel;
    p.grinderBurrs = grinderBurrs;
    p.grinderSetting = grinderSetting;
    p.beanBrand = beanBrand;
    p.beanType = beanType;
    return p;
}

} // namespace

class tst_AIManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Isolate the conversation index from the real user dir so loading /
        // saving doesn't mutate state outside the test.
        QStandardPaths::setTestModeEnabled(true);
    }

    // Task 10.5 end-to-end: the assembled payload from emitRecentShotContext
    // contains exactly one ### Profile: header (with intent + recipe), exactly
    // one ### Setup: header, and the per-shot blocks render in HistoryBlock
    // mode (no per-shot ## Shot Summary header, no per-shot Profile/Setup
    // duplicates).
    void emitRecentShotContext_hoistsProfileAndSetupOnce()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        // Match the serial counter so the staleness gate doesn't suppress.
        mgr.m_contextSerial = 1;

        const QString intent = QStringLiteral("0.5–1.2 ml/s target through extraction");
        // Frames-style profile JSON so describeFramesFromJson parses cleanly
        // and the recipe block renders.
        const QString profileJson = QStringLiteral(R"({
            "title": "80's Espresso",
            "type": "advanced",
            "version": 2,
            "steps": [
                {"name":"preinfusion","temperature":92,"seconds":8,"flow":4.0,"transition":"fast","exit":{"type":"pressure_over","value":4.0}},
                {"name":"pour","temperature":92,"seconds":22,"pressure":9.0,"transition":"smooth"}
            ]
        })");

        QList<QPair<qint64, ShotProjection>> qualifiedShots;
        const qint64 base = QDateTime::currentSecsSinceEpoch() - 86400 * 4;
        for (int i = 0; i < 4; ++i) {
            qualifiedShots.append({
                base + i * 3600,
                makeShot(i + 1, base + i * 3600,
                         QStringLiteral("Niche"),
                         QStringLiteral("Zero"),
                         QStringLiteral("63mm Mazzer Kony conical"),
                         QString::number(4.0 + i * 0.1),
                         QStringLiteral("Northbound Coffee Roasters"),
                         QStringLiteral("Spring Tour 2026 #2"),
                         QStringLiteral("80's Espresso"),
                         intent,
                         profileJson)
            });
        }

        QSignalSpy spy(&mgr, &AIManager::recentShotContextReady);
        QVERIFY(spy.isValid());

        mgr.emitRecentShotContext(qualifiedShots, GrinderContext{}, QStringLiteral("Niche"), 1);

        QCOMPARE(spy.count(), 1);
        const QString payload = spy.takeFirst().at(0).toString();
        QVERIFY2(!payload.isEmpty(), "payload must not be empty for a populated 4-shot history");

        // Profile + Setup headers each appear exactly once.
        QCOMPARE(payload.count(QStringLiteral("### Profile: 80's Espresso")), 1);
        QCOMPARE(payload.count(QStringLiteral("### Setup:")), 1);

        // The intent paragraph appears exactly once (hoisted) — not 4×.
        QCOMPARE(payload.count(intent), 1);

        // Setup header carries grinder + bean identity.
        QVERIFY2(payload.contains(QStringLiteral("### Setup: Niche Zero with 63mm Mazzer Kony conical on Northbound Coffee Roasters - Spring Tour 2026 #2")),
                 "Setup header must combine grinder + bean identity");

        // Per-shot blocks render in HistoryBlock mode — no ## Shot Summary headers.
        QCOMPARE(payload.count(QStringLiteral("## Shot Summary")), 0);

        // Per-shot blocks must not carry the profile intent again (would mean
        // HistoryBlock mode regressed to Standalone).
        QVERIFY2(!payload.contains(QStringLiteral("**Profile intent**:")),
                 "per-shot blocks must not carry Profile intent: lines");
    }

    // Empty grinder/bean fields on later shots must be treated as
    // "unrecorded, inherit" — not "different" — so the Setup header stays
    // populated for histories that mix pre-DYE and post-DYE shots. This
    // pins the fix for the setupShared empty-vs-populated comparison flagged
    // in PR review of #1030.
    void emitRecentShotContext_legacyEmptyShotDoesNotSuppressSetup()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);
        mgr.m_contextSerial = 7;

        const qint64 base = QDateTime::currentSecsSinceEpoch() - 86400 * 4;
        QList<QPair<qint64, ShotProjection>> qualifiedShots;
        // Shot 0: fully recorded (post-DYE).
        qualifiedShots.append({
            base + 3 * 3600,
            makeShot(1, base + 3 * 3600,
                     QStringLiteral("Niche"), QStringLiteral("Zero"),
                     QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
                     QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
                     QStringLiteral("80's Espresso"), QStringLiteral("intent"),
                     QString())
        });
        // Shot 1: legacy unrecorded grinder/bean (pre-DYE).
        qualifiedShots.append({
            base + 2 * 3600,
            makeShot(2, base + 2 * 3600,
                     QString(), QString(), QString(), QStringLiteral("4.0"),
                     QString(), QString(),
                     QStringLiteral("80's Espresso"), QString(), QString())
        });

        QSignalSpy spy(&mgr, &AIManager::recentShotContextReady);
        mgr.emitRecentShotContext(qualifiedShots, GrinderContext{}, QStringLiteral("Niche"), 7);

        QCOMPARE(spy.count(), 1);
        const QString payload = spy.takeFirst().at(0).toString();

        // Legacy empty shot must NOT flip setupShared to false — the Setup
        // header should still emit with shot[0]'s recorded identity.
        QCOMPARE(payload.count(QStringLiteral("### Setup:")), 1);
        QVERIFY2(payload.contains(QStringLiteral("Niche Zero with 63mm Kony")),
                 "Setup header must carry the recorded grinder identity even when later shots are blank");
    }

    // A genuine identity conflict (two shots with different non-empty grinder
    // brands) must suppress the Setup header — regression guard in case the
    // empty-string fix above accidentally swallows real mismatches.
    void emitRecentShotContext_genuineConflictSuppressesSetup()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);
        mgr.m_contextSerial = 9;

        const qint64 base = QDateTime::currentSecsSinceEpoch() - 86400 * 4;
        QList<QPair<qint64, ShotProjection>> qualifiedShots;
        qualifiedShots.append({
            base + 2 * 3600,
            makeShot(1, base + 2 * 3600,
                     QStringLiteral("Niche"), QStringLiteral("Zero"),
                     QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
                     QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
                     QStringLiteral("80's Espresso"), QString(), QString())
        });
        qualifiedShots.append({
            base + 1 * 3600,
            makeShot(2, base + 1 * 3600,
                     QStringLiteral("Eureka"), QStringLiteral("Atom 75"),
                     QStringLiteral("75mm flat"), QStringLiteral("3.5"),
                     QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
                     QStringLiteral("80's Espresso"), QString(), QString())
        });

        QSignalSpy spy(&mgr, &AIManager::recentShotContextReady);
        mgr.emitRecentShotContext(qualifiedShots, GrinderContext{}, QStringLiteral("Niche"), 9);

        QCOMPARE(spy.count(), 1);
        const QString payload = spy.takeFirst().at(0).toString();

        QCOMPARE(payload.count(QStringLiteral("### Setup:")), 0);
    }

    // The Setup header builder must produce clean prose for partial-DYE
    // shapes — no double spaces, no trailing/leading separators, no "on"
    // before an empty bean name. Regression guard for the multi-segment
    // join introduced post-#1030.
    void emitRecentShotContext_setupHeader_partialFieldShapes_data()
    {
        QTest::addColumn<QString>("grinderBrand");
        QTest::addColumn<QString>("grinderModel");
        QTest::addColumn<QString>("grinderBurrs");
        QTest::addColumn<QString>("beanBrand");
        QTest::addColumn<QString>("beanType");
        QTest::addColumn<QString>("expectedSetupLine");

        // Full identity (sanity baseline).
        QTest::newRow("full")
            << "Niche" << "Zero" << "63mm Kony" << "Northbound" << "Spring Tour"
            << "### Setup: Niche Zero with 63mm Kony on Northbound - Spring Tour";
        // Burrs without grinder brand+model (rare but possible if user clears
        // brand/model after entering burrs). Pre-fix this rendered with a
        // double-space artifact: `### Setup:  with 63mm`.
        QTest::newRow("burrsOnly")
            << "" << "" << "63mm Kony" << "Northbound" << "Spring Tour"
            << "### Setup: 63mm Kony on Northbound - Spring Tour";
        // Bean type only (cultivar without roaster name).
        QTest::newRow("beanTypeOnly")
            << "Niche" << "Zero" << "63mm Kony" << "" << "Spring Tour"
            << "### Setup: Niche Zero with 63mm Kony on Spring Tour";
        // Bean brand only (no specific cultivar).
        QTest::newRow("beanBrandOnly")
            << "Niche" << "Zero" << "63mm Kony" << "Northbound" << ""
            << "### Setup: Niche Zero with 63mm Kony on Northbound";
        // Grinder brand only — no model, no burrs.
        QTest::newRow("grinderBrandOnly")
            << "Niche" << "" << "" << "Northbound" << "Spring Tour"
            << "### Setup: Niche on Northbound - Spring Tour";
        // Grinder model only — no brand, no burrs.
        QTest::newRow("grinderModelOnly")
            << "" << "Zero" << "" << "Northbound" << "Spring Tour"
            << "### Setup: Zero on Northbound - Spring Tour";
        // Grinder identity only — no bean fields at all.
        QTest::newRow("grinderOnly")
            << "Niche" << "Zero" << "63mm Kony" << "" << ""
            << "### Setup: Niche Zero with 63mm Kony";
        // Bean only — no grinder fields at all.
        QTest::newRow("beanOnly")
            << "" << "" << "" << "Northbound" << "Spring Tour"
            << "### Setup: Northbound - Spring Tour";
    }
    void emitRecentShotContext_setupHeader_partialFieldShapes()
    {
        QFETCH(QString, grinderBrand);
        QFETCH(QString, grinderModel);
        QFETCH(QString, grinderBurrs);
        QFETCH(QString, beanBrand);
        QFETCH(QString, beanType);
        QFETCH(QString, expectedSetupLine);

        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);
        mgr.m_contextSerial = 11;

        const qint64 base = QDateTime::currentSecsSinceEpoch() - 3600;
        QList<QPair<qint64, ShotProjection>> qualifiedShots;
        qualifiedShots.append({
            base,
            makeShot(1, base, grinderBrand, grinderModel, grinderBurrs,
                     QStringLiteral("4.0"), beanBrand, beanType,
                     QStringLiteral("Profile"), QString(), QString())
        });

        QSignalSpy spy(&mgr, &AIManager::recentShotContextReady);
        mgr.emitRecentShotContext(qualifiedShots, GrinderContext{}, grinderBrand, 11);

        QCOMPARE(spy.count(), 1);
        const QString payload = spy.takeFirst().at(0).toString();

        QVERIFY2(payload.contains(expectedSetupLine),
                 qPrintable(QString("expected '%1' in payload, got: %2")
                                .arg(expectedSetupLine)
                                .arg(payload.left(500))));
        // Defensive: no double-space artifacts anywhere in the Setup line.
        const int setupStart = payload.indexOf(QStringLiteral("### Setup:"));
        QVERIFY(setupStart >= 0);
        const int setupEnd = payload.indexOf(QChar('\n'), setupStart);
        const QString setupLine = payload.mid(setupStart, setupEnd - setupStart);
        QVERIFY2(!setupLine.contains(QStringLiteral("  ")),
                 qPrintable("Setup line has double space: " + setupLine));
    }

    // Stale serial — a request that's been superseded by a newer one — emits
    // an empty string so QML clears its contextLoading flag.
    void emitRecentShotContext_staleSerialEmitsEmpty()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);
        mgr.m_contextSerial = 5;

        QList<QPair<qint64, ShotProjection>> qualifiedShots;
        qualifiedShots.append({
            42,
            makeShot(1, 42, QStringLiteral("Niche"), QStringLiteral("Zero"),
                     QStringLiteral("63mm"), QStringLiteral("4.0"),
                     QStringLiteral("Bean"), QStringLiteral("Type"),
                     QStringLiteral("Profile"), QString(), QString())
        });

        QSignalSpy spy(&mgr, &AIManager::recentShotContextReady);
        // Caller's serial (3) doesn't match the current serial (5).
        mgr.emitRecentShotContext(qualifiedShots, GrinderContext{}, QStringLiteral("Niche"), 3);

        QCOMPARE(spy.count(), 1);
        QVERIFY2(spy.takeFirst().at(0).toString().isEmpty(),
                 "stale request must emit empty string");
    }
};

QTEST_GUILESS_MAIN(tst_AIManager)

#include "tst_aimanager.moc"
