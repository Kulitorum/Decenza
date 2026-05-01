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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>

#include "ai/aimanager.h"
#include "ai/aiconversation.h"
#include "core/settings.h"
#include "core/settings_dye.h"
#include "history/shotprojection.h"
#include "history/shothistory_types.h"
#include "ai/dialing_blocks.h"

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
                {"name":"preinfusion","temperature":92,"seconds":8,"flow":4.0,"transition":"fast","exit":{"type":"pressure","condition":"over","value":4.0}},
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
        // Burrs recorded without grinder brand+model (rare but possible if
        // user clears brand/model after entering burrs). Pre-fix this
        // rendered with a double-space artifact: `### Setup:  with 63mm`.
        QTest::newRow("burrsNoGrinderName")
            << "" << "" << "63mm Kony" << "Northbound" << "Spring Tour"
            << "### Setup: 63mm Kony on Northbound - Spring Tour";
        // Cultivar entered without roaster brand (full grinder identity).
        QTest::newRow("beanTypeNoBrand")
            << "Niche" << "Zero" << "63mm Kony" << "" << "Spring Tour"
            << "### Setup: Niche Zero with 63mm Kony on Spring Tour";
        // Roaster entered without specific cultivar (full grinder identity).
        QTest::newRow("beanBrandNoType")
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
        const qsizetype setupStart = payload.indexOf(QStringLiteral("### Setup:"));
        QVERIFY(setupStart >= 0);
        const qsizetype setupEnd = payload.indexOf(QChar('\n'), setupStart);
        const QString setupLine = payload.mid(setupStart, setupEnd - setupStart);
        QVERIFY2(!setupLine.contains(QStringLiteral("  ")),
                 qPrintable("Setup line has double space: " + setupLine));
    }

    // ---------------------------------------------------------------------
    // openspec add-dialing-blocks-to-advisor — user-prompt envelope
    //
    // Pins the contract that buildUserPromptObjectForShot returns the
    // canonical four-key envelope (currentBean / profile / tastingFeedback /
    // shotAnalysis) without any of the four DB-scoped enrichment keys that
    // the in-app advisor's bg-thread closure layers on. Synchronous callers
    // (`generateEmailPrompt`, `generateShotSummary`,
    // `generateHistoryShotSummary`) never see those four enrichment keys —
    // they're added by callers with DB scope, not by ShotSummarizer itself.
    // ---------------------------------------------------------------------
    void buildUserPromptObjectForShot_carriesCanonicalEnvelope()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonObject obj = mgr.buildUserPromptObjectForShot(shot);
        QVERIFY(obj.contains(QStringLiteral("currentBean")));
        QVERIFY(obj.contains(QStringLiteral("profile")));
        QVERIFY(obj.contains(QStringLiteral("tastingFeedback")));
        QVERIFY(obj.contains(QStringLiteral("shotAnalysis")));
    }

    void buildUserPromptObjectForShot_omitsDialingEnrichmentKeys()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonObject obj = mgr.buildUserPromptObjectForShot(shot);
        // The four DB-scoped enrichment keys are layered on by callers with
        // DB scope (the in-app advisor's bg-thread closure,
        // ai_advisor_invoke). They MUST NOT come from the synchronous
        // envelope builder, otherwise we'd be shipping nulls or stale data.
        QVERIFY2(!obj.contains(QStringLiteral("dialInSessions")),
                 "dialInSessions must be added by DB-scoped callers, not the envelope builder");
        QVERIFY2(!obj.contains(QStringLiteral("bestRecentShot")),
                 "bestRecentShot must be added by DB-scoped callers, not the envelope builder");
        QVERIFY2(!obj.contains(QStringLiteral("grinderContext")),
                 "grinderContext must be added by DB-scoped callers, not the envelope builder");
        QVERIFY2(!obj.contains(QStringLiteral("sawPrediction")),
                 "sawPrediction must be added by DB-scoped callers, not the envelope builder");
    }

    // Cache stability invariant: the user prompt envelope must not embed any
    // wall-clock value that varies per call. `currentDateTime` (the field
    // dialing_get_context's response carries at the top level) MUST NOT
    // appear in the user prompt — including it would bust the prompt cache
    // on every multi-turn follow-up.
    void buildUserPromptObjectForShot_omitsCurrentDateTime()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonObject obj = mgr.buildUserPromptObjectForShot(shot);
        const QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        QVERIFY2(!obj.contains(QStringLiteral("currentDateTime")),
                 "user prompt must not carry a top-level currentDateTime key");
        QVERIFY2(!json.contains(QStringLiteral("currentDateTime")),
                 "no currentDateTime substring anywhere in serialized prompt");
    }

    // Two calls with identical state produce byte-identical envelopes —
    // load-bearing precondition for Anthropic's prompt cache to hit on
    // multi-turn follow-ups.
    void buildUserPromptObjectForShot_byteStableAcrossCalls()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(42, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QString a = QString::fromUtf8(
            QJsonDocument(mgr.buildUserPromptObjectForShot(shot)).toJson(QJsonDocument::Indented));
        const QString b = QString::fromUtf8(
            QJsonDocument(mgr.buildUserPromptObjectForShot(shot)).toJson(QJsonDocument::Indented));
        QCOMPARE(a, b);
    }

    // ---------------------------------------------------------------------
    // Both surfaces produce byte-equivalent `currentBean` JSON for the
    // same resolved shot. The MCP path
    // (`dialing_get_context.currentBean`) and the in-app advisor's
    // user-prompt path
    // (`AIManager::buildUserPromptObjectForShot(...)["currentBean"]`)
    // build through the shared
    // `DialingBlocks::buildCurrentBeanBlock`, sourced solely from the
    // resolved shot. Pinned end-to-end so future drift between the two
    // builders fails the test rather than confusing the LLM with two
    // disagreeing views of the same shot.
    // ---------------------------------------------------------------------
    void currentBean_equivalenceAcrossSurfaces()
    {
        QNetworkAccessManager nam;
        // Live DYE state is deliberately divergent from the shot's saved
        // metadata to model the case where the user changed DYE between
        // pulling the shot and asking the AI about it. currentBean must
        // NOT pick up the live DYE values on either surface — the shot is
        // the source of truth.
        Settings settings;
        settings.dye()->setDyeBeanBrand(QStringLiteral("Live DYE Brand"));
        settings.dye()->setDyeBeanType(QStringLiteral("Live DYE Type"));
        settings.dye()->setDyeRoastLevel(QStringLiteral("Light"));
        settings.dye()->setDyeGrinderBrand(QStringLiteral("Live DYE Grinder"));
        settings.dye()->setDyeGrinderModel(QStringLiteral("Live DYE Model"));
        settings.dye()->setDyeGrinderBurrs(QStringLiteral("Live DYE Burrs"));
        settings.dye()->setDyeGrinderSetting(QStringLiteral("99"));
        settings.dye()->setDyeBeanWeight(99.0);
        settings.dye()->setDyeRoastDate(QStringLiteral("2025-01-01"));

        AIManager mgr(&nam, &settings);

        // Shot has its own bean / grinder / dose / roastDate that
        // currentBean must echo on every surface.
        ShotProjection shot = makeShot(884, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.5"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour 2026 #2"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());
        shot.doseWeightG = 20.0;
        shot.roastLevel = QStringLiteral("Dark");
        shot.roastDate = QStringLiteral("2026-03-30");

        // In-app advisor surface: through ShotSummarizer::buildUserPromptObject
        // off summarizeFromHistory(shot).
        const QJsonObject inAppEnvelope = mgr.buildUserPromptObjectForShot(shot);
        QVERIFY(inAppEnvelope.contains(QStringLiteral("currentBean")));
        const QJsonObject inAppCurrentBean = inAppEnvelope.value(QStringLiteral("currentBean")).toObject();

        // MCP surface: the same shared helper that mcptools_dialing.cpp
        // calls on the resolved shot (mirrors the
        // `mcptools_dialing.cpp:200`-block exactly — same field-by-field
        // mapping from `sd` (the resolved shot) into
        // `CurrentBeanBlockInputs`).
        DialingBlocks::CurrentBeanBlockInputs in;
        in.beanBrand = shot.beanBrand;
        in.beanType = shot.beanType;
        in.roastLevel = shot.roastLevel;
        in.roastDate = shot.roastDate;
        in.grinderBrand = shot.grinderBrand;
        in.grinderModel = shot.grinderModel;
        in.grinderBurrs = shot.grinderBurrs;
        in.grinderSetting = shot.grinderSetting;
        in.doseWeightG = shot.doseWeightG;
        const QJsonObject mcpCurrentBean = DialingBlocks::buildCurrentBeanBlock(in);

        // The contract: byte-equivalent JSON for the same shot.
        QCOMPARE(inAppCurrentBean, mcpCurrentBean);

        // Spot-check the shot values won the source-of-truth contest
        // against the live DYE values, on both surfaces.
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("type")).toString(),
                 QStringLiteral("Spring Tour 2026 #2"));
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("roastLevel")).toString(),
                 QStringLiteral("Dark"));
        QCOMPARE(inAppCurrentBean.value(QStringLiteral("doseWeightG")).toDouble(), 20.0);

        // Inferred-field machinery is gone on both surfaces.
        QVERIFY(!inAppCurrentBean.contains(QStringLiteral("inferredFields")));
        QVERIFY(!inAppCurrentBean.contains(QStringLiteral("inferredFromShotId")));
        QVERIFY(!inAppCurrentBean.contains(QStringLiteral("inferredNote")));
        QVERIFY(!mcpCurrentBean.contains(QStringLiteral("inferredFields")));
        QVERIFY(!mcpCurrentBean.contains(QStringLiteral("inferredFromShotId")));

        // beanFreshness reads from the shot's roastDate, not live DYE's.
        QVERIFY(inAppCurrentBean.contains(QStringLiteral("beanFreshness")));
        const QJsonObject freshness =
            inAppCurrentBean.value(QStringLiteral("beanFreshness")).toObject();
        QCOMPARE(freshness.value(QStringLiteral("roastDate")).toString(),
                 QStringLiteral("2026-03-30"));
    }

    // ---------------------------------------------------------------------
    // openspec drop-nested-envelope-in-dialing-shot-analysis — pin that
    // `dialing_get_context.shotAnalysis` is prose-only (no nested JSON
    // envelope) and that the prose matches the in-app advisor's user-
    // prompt envelope's `shotAnalysis` field byte-for-byte.
    // ---------------------------------------------------------------------
    void buildShotAnalysisProseForShot_returnsProseNotJson()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, QDateTime::currentSecsSinceEpoch(),
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QString prose = mgr.buildShotAnalysisProseForShot(shot);
        QVERIFY(!prose.isEmpty());

        // Prose body — starts with the Shot Summary header, contains the
        // Phase Data block.
        QVERIFY2(prose.contains(QStringLiteral("## Shot Summary")),
                 "prose body must carry the Shot Summary header");
        QVERIFY2(prose.contains(QStringLiteral("## Phase Data")),
                 "prose body must carry the Phase Data header");

        // Not a JSON envelope — must NOT carry the structured-field
        // block names that the previous nested envelope embedded.
        QVERIFY2(!prose.contains(QStringLiteral("\"currentBean\"")),
                 "prose body must not embed a JSON currentBean block");
        QVERIFY2(!prose.contains(QStringLiteral("\"tastingFeedback\"")),
                 "prose body must not embed a JSON tastingFeedback block");
        QVERIFY2(!prose.contains(QStringLiteral("\"profile\":")),
                 "prose body must not embed a JSON profile block");

        // Parsing the prose as JSON should not yield an object — it's a
        // markdown string, not a JSON-encoded envelope.
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(prose.toUtf8(), &err);
        QVERIFY2(err.error != QJsonParseError::NoError || !doc.isObject(),
                 "prose body must not parse as a JSON object");
    }

    void buildShotAnalysisProseForShot_matchesEnvelopeShotAnalysisField()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(42, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        // The prose returned by buildShotAnalysisProseForShot MUST be the
        // same string the user-prompt envelope carries under its
        // `shotAnalysis` key — they share the private renderer, and any
        // future drift would re-introduce the bug this change retired.
        const QString prose = mgr.buildShotAnalysisProseForShot(shot);
        const QJsonObject envelope = mgr.buildUserPromptObjectForShot(shot);
        const QString envelopeShotAnalysis = envelope.value(QStringLiteral("shotAnalysis")).toString();

        QCOMPARE(prose, envelopeShotAnalysis);
    }

    // ---------------------------------------------------------------------
    // enrichUserPromptObject — single-source merge step shared by the in-app
    // advisor and ai_advisor_invoke. Pins that the four blocks land at the
    // right keys, that empty blocks are suppressed (no nulls), and that the
    // merged envelope is byte-stable across calls.
    // ---------------------------------------------------------------------
    void enrichUserPromptObject_mergesAllFourBlocks()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        QJsonObject payload = mgr.buildUserPromptObjectForShot(shot);

        // Synthetic blocks — the merge step is what's under test, not the
        // bg-thread DB builders. SAW is omitted by the helper (no flow data
        // on this minimal shot), which is the correct behavior.
        const QJsonArray dialInSessions{
            QJsonObject{{"sessionStart", "2026-04-29T09:29:19-06:00"},
                        {"shotCount", 1}}};
        const QJsonObject bestRecentShot{{"id", 42}, {"enjoyment0to100", 85}};
        const QJsonObject grinderContext{{"model", "Zero"}, {"smallestStep", 0.25}};

        mgr.enrichUserPromptObject(payload, shot, dialInSessions, bestRecentShot, grinderContext);

        QVERIFY(payload.contains(QStringLiteral("dialInSessions")));
        QVERIFY(payload.contains(QStringLiteral("bestRecentShot")));
        QVERIFY(payload.contains(QStringLiteral("grinderContext")));
        // SAW correctly suppressed — no flow data on a synthetic ShotProjection.
        QVERIFY2(!payload.contains(QStringLiteral("sawPrediction")),
                 "SAW must be suppressed when ShotProjection has no usable flow data");

        // Original four-key envelope still intact under the new keys.
        QVERIFY(payload.contains(QStringLiteral("currentBean")));
        QVERIFY(payload.contains(QStringLiteral("profile")));
        QVERIFY(payload.contains(QStringLiteral("tastingFeedback")));
        QVERIFY(payload.contains(QStringLiteral("shotAnalysis")));
    }

    void enrichUserPromptObject_suppressesEmptyBlocks()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        QJsonObject payload = mgr.buildUserPromptObjectForShot(shot);

        // All blocks empty — none of the four enrichment keys should appear,
        // and crucially no `null` placeholders. dialing_get_context's omission
        // contract requires absent key, not `null`.
        mgr.enrichUserPromptObject(payload, shot,
            QJsonArray{}, QJsonObject{}, QJsonObject{});

        QVERIFY2(!payload.contains(QStringLiteral("dialInSessions")),
                 "empty dialInSessions must not be added as a key");
        QVERIFY2(!payload.contains(QStringLiteral("bestRecentShot")),
                 "empty bestRecentShot must not be added as a key");
        QVERIFY2(!payload.contains(QStringLiteral("grinderContext")),
                 "empty grinderContext must not be added as a key");
        QVERIFY2(!payload.contains(QStringLiteral("sawPrediction")),
                 "empty sawPrediction must not be added as a key");

        // Serialized output also free of the keys (no `null`-shaped JSON).
        const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QVERIFY(!json.contains(QStringLiteral("dialInSessions")));
        QVERIFY(!json.contains(QStringLiteral("bestRecentShot")));
        QVERIFY(!json.contains(QStringLiteral("grinderContext")));
        QVERIFY(!json.contains(QStringLiteral("sawPrediction")));
    }

    void enrichUserPromptObject_byteStableAcrossCalls()
    {
        QNetworkAccessManager nam;
        Settings settings;
        AIManager mgr(&nam, &settings);

        const ShotProjection shot = makeShot(42, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm Kony"), QStringLiteral("4.0"),
            QStringLiteral("Northbound"), QStringLiteral("Spring Tour"),
            QStringLiteral("80's Espresso"), QStringLiteral("intent"), QString());

        const QJsonArray dialInSessions{QJsonObject{{"shotCount", 2}}};
        const QJsonObject bestRecentShot{{"id", 7}};
        const QJsonObject grinderContext{{"model", "Zero"}};

        QJsonObject a = mgr.buildUserPromptObjectForShot(shot);
        mgr.enrichUserPromptObject(a, shot, dialInSessions, bestRecentShot, grinderContext);

        QJsonObject b = mgr.buildUserPromptObjectForShot(shot);
        mgr.enrichUserPromptObject(b, shot, dialInSessions, bestRecentShot, grinderContext);

        const QString jsonA = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Indented));
        const QString jsonB = QString::fromUtf8(QJsonDocument(b).toJson(QJsonDocument::Indented));
        QCOMPARE(jsonA, jsonB);
    }

    // ---------------------------------------------------------------------
    // DialingBlocks gating — preconditions that short-circuit before
    // touching the DB / Settings / ProfileManager. These cases ship the
    // omission contract (empty QJsonObject so callers suppress the key)
    // without needing real DB infrastructure.
    // ---------------------------------------------------------------------
    void sawPredictionBlock_omittedWhenSettingsNull()
    {
        // Espresso shot WITH flow data so we get past the espresso and flow
        // gates, then assert the settings-null gate fires.
        ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm"), QStringLiteral("4.0"),
            QStringLiteral("Bean"), QStringLiteral("Type"),
            QStringLiteral("Profile"), QString(), QString());
        shot.flow = QVariantList{
            QVariantMap{{"x", 28.0}, {"y", 1.8}},
            QVariantMap{{"x", 29.0}, {"y", 2.0}},
            QVariantMap{{"x", 30.0}, {"y", 2.1}}};
        const QJsonObject sp = DialingBlocks::buildSawPredictionBlock(nullptr, nullptr, shot);
        QVERIFY(sp.isEmpty());
    }

    void sawPredictionBlock_omittedForNonEspresso()
    {
        // Provide flow data so the flow gate would not fire — the
        // beverage-type gate must be what produces the empty result.
        ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm"), QStringLiteral("4.0"),
            QStringLiteral("Bean"), QStringLiteral("Type"),
            QStringLiteral("Profile"), QString(), QString());
        shot.beverageType = QStringLiteral("filter");
        shot.flow = QVariantList{
            QVariantMap{{"x", 28.0}, {"y", 4.0}},
            QVariantMap{{"x", 29.0}, {"y", 4.2}},
            QVariantMap{{"x", 30.0}, {"y", 4.1}}};
        Settings settings;
        const QJsonObject sp = DialingBlocks::buildSawPredictionBlock(&settings, nullptr, shot);
        QVERIFY(sp.isEmpty());
    }

    void sawPredictionBlock_omittedWhenFlowAtCutoffIsZero()
    {
        // Espresso shot, empty flow samples → estimateFlowAtCutoff returns
        // 0 → flow gate fires before the settings/profileManager gates can
        // be evaluated.
        const ShotProjection shot = makeShot(1, 1700000000,
            QStringLiteral("Niche"), QStringLiteral("Zero"),
            QStringLiteral("63mm"), QStringLiteral("4.0"),
            QStringLiteral("Bean"), QStringLiteral("Type"),
            QStringLiteral("Profile"), QString(), QString());
        Settings settings;
        const QJsonObject sp = DialingBlocks::buildSawPredictionBlock(&settings, nullptr, shot);
        QVERIFY(sp.isEmpty());
    }

    void dialInSessionsBlock_returnsEmpty_whenProfileKbIdEmpty()
    {
        // Pass an unopened DB ref — the empty-kbId guard short-circuits
        // before any DB access. (We can't easily stand up a real DB here;
        // this test pins the gating, not the DB query path.)
        QSqlDatabase db; // default-constructed: invalid, never used
        const QJsonArray arr = DialingBlocks::buildDialInSessionsBlock(
            db, QString(), 1, 5);
        QVERIFY(arr.isEmpty());
    }

    void bestRecentShotBlock_returnsEmpty_whenProfileKbIdEmpty()
    {
        QSqlDatabase db;
        ShotProjection shot;
        const QJsonObject obj = DialingBlocks::buildBestRecentShotBlock(
            db, QString(), 1, shot);
        QVERIFY(obj.isEmpty());
    }

    void grinderContextBlock_returnsEmpty_whenGrinderModelEmpty()
    {
        QSqlDatabase db;
        const QJsonObject obj = DialingBlocks::buildGrinderContextBlock(
            db, QString(), QStringLiteral("espresso"), QString());
        QVERIFY(obj.isEmpty());
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

    // =====================================================================
    // AIConversation::extractShotFields — issue #1039
    // Pins the structured-field migration: dose / yield / duration /
    // grinder / score / notes are now read directly from the JSON
    // envelope's `shot`, `currentBean`, and `profile` blocks. Legacy
    // stored conversations whose user messages predate the JSON
    // envelope still resolve via a regex fallback path.
    //
    // Friend-class access (`friend class tst_AIManager` under
    // DECENZA_TESTING) lets these tests reach the private static
    // helper without instantiating an AIConversation.
    // =====================================================================
    void aiConversation_extractShotFields_structuredEnvelope_readsCanonicalKeys()
    {
        const QString content = QStringLiteral(
            "## Shot (2026-05-01 14:30)\n\n"
            "Here's my latest shot:\n\n"
            "{"
            "  \"currentBean\": {"
            "    \"grinderBrand\": \"Niche\","
            "    \"grinderModel\": \"Zero\","
            "    \"grinderBurrs\": \"63mm\","
            "    \"doseWeightG\": 18.0"
            "  },"
            "  \"profile\": {\"title\": \"80's Espresso\"},"
            "  \"shot\": {"
            "    \"doseG\": 18.0,"
            "    \"yieldG\": 36.0,"
            "    \"durationSec\": 30.0,"
            "    \"grinderSetting\": \"4.0\","
            "    \"enjoyment0to100\": 85,"
            "    \"notes\": \"balanced\""
            "  },"
            "  \"shotAnalysis\": \"## Shot Summary\\n- Dose: 18g, etc.\""
            "}\n\nPlease analyze.");

        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.fromStructuredEnvelope);
        QCOMPARE(fields.shotLabel, QStringLiteral("2026-05-01 14:30"));
        QCOMPARE(fields.doseG, QStringLiteral("18.0"));
        QCOMPARE(fields.yieldG, QStringLiteral("36.0"));
        QCOMPARE(fields.durationSec, QStringLiteral("30"));
        QCOMPARE(fields.score, QStringLiteral("85"));
        QCOMPARE(fields.notes, QStringLiteral("balanced"));
        QCOMPARE(fields.profileTitle, QStringLiteral("80's Espresso"));
        // Format mirrors the legacy prose ("<brand> <model> with <burrs>
        // @ <setting>") so cross-era conversations (one shot's grinder
        // captured by regex from prose, the next from JSON) do not
        // emit spurious "grinder changed" diffs.
        QCOMPARE(fields.grinder, QStringLiteral("Niche Zero with 63mm @ 4.0"));
    }

    void aiConversation_extractShotFields_detectorFlagsEchoFromShotAnalysisProse()
    {
        const QString content = QStringLiteral(
            "## Shot (2026-05-01)\n\nHere's my latest shot:\n\n"
            "{"
            "  \"shot\": {\"doseG\": 18.0, \"yieldG\": 36.0},"
            "  \"shotAnalysis\": \"## Shot Summary\\nChanneling detected during pour.\\nTemperature unstable in phase 2.\""
            "}\n\nWhat to do?");

        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.fromStructuredEnvelope);
        QVERIFY(fields.channelingDetected);
        QVERIFY(fields.temperatureUnstable);
    }

    void aiConversation_extractShotFields_legacyProseFallsBackToRegex()
    {
        const QString content = QStringLiteral(
            "## Shot (2025-12-15 09:00)\n\nHere's my latest shot:\n\n"
            "## Shot Summary\n"
            "- **Dose**: 18.0g \xe2\x86\x92 **Yield**: 36.0g ratio 1:2.0\n"
            "- **Duration**: 30s\n"
            "- **Grinder**: Niche Zero\n"
            "- **Profile**: 80's Espresso\n"
            "- **Score**: 85\n"
            "- **Notes**: \"balanced\"\n"
            "Channeling detected during pour.\n");

        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY2(!fields.fromStructuredEnvelope,
                 "legacy prose must report regex-fallback path");
        QCOMPARE(fields.shotLabel, QStringLiteral("2025-12-15 09:00"));
        QCOMPARE(fields.doseG, QStringLiteral("18.0"));
        QCOMPARE(fields.yieldG, QStringLiteral("36.0"));
        QCOMPARE(fields.durationSec, QStringLiteral("30"));
        QCOMPARE(fields.score, QStringLiteral("85"));
        QCOMPARE(fields.notes, QStringLiteral("balanced"));
        QCOMPARE(fields.grinder, QStringLiteral("Niche Zero"));
        QCOMPARE(fields.profileTitle, QStringLiteral("80's Espresso"));
        QVERIFY(fields.channelingDetected);
        QVERIFY(!fields.temperatureUnstable);
    }

    // Cross-era equivalence: the structured path produces the same
    // grinder string the legacy regex would have captured from the old
    // prose body. Both inputs use the production-historic prose format
    // ("**Grinder**: <brand> <model> with <burrs> @ <setting>") so a
    // conversation that spans both eras (older shot regex-extracted,
    // newer shot structured) does not emit spurious grinder-change
    // diffs. Critically, the legacy input is the format the regex
    // *actually* sees in stored conversations from before #1041.
    void aiConversation_extractShotFields_grinderStringMatchesLegacyProseFormat()
    {
        const QString legacyProse = QStringLiteral(
            "## Shot Summary\n"
            "- **Grinder**: Niche Zero with 63mm conical @ 4.5\n");
        const QString structuredEnvelope = QStringLiteral(
            "{"
            "  \"currentBean\": {"
            "    \"grinderBrand\": \"Niche\","
            "    \"grinderModel\": \"Zero\","
            "    \"grinderBurrs\": \"63mm conical\""
            "  },"
            "  \"shot\": {\"grinderSetting\": \"4.5\"}"
            "}");

        const auto legacyFields = AIConversation::extractShotFields(legacyProse);
        const auto structuredFields = AIConversation::extractShotFields(structuredEnvelope);

        QVERIFY(!legacyFields.fromStructuredEnvelope);
        QVERIFY(structuredFields.fromStructuredEnvelope);
        QCOMPARE(structuredFields.grinder, legacyFields.grinder);
        QCOMPARE(structuredFields.grinder,
                 QStringLiteral("Niche Zero with 63mm conical @ 4.5"));
    }

    void aiConversation_extractShotFields_normalizesNumericPrecision()
    {
        const QString content = QStringLiteral(
            "{\"shot\": {\"doseG\": 18, \"yieldG\": 36, \"durationSec\": 27}}");
        const auto fields = AIConversation::extractShotFields(content);
        QCOMPARE(fields.doseG, QStringLiteral("18.0"));
        QCOMPARE(fields.yieldG, QStringLiteral("36.0"));
        QCOMPARE(fields.durationSec, QStringLiteral("27"));
    }

    void aiConversation_extractShotFields_emptyShotProducesEmptyFields()
    {
        const QString content = QStringLiteral("{\"shotAnalysis\": \"## Shot Summary\\n\"}");
        const auto fields = AIConversation::extractShotFields(content);
        QVERIFY(fields.fromStructuredEnvelope);
        QVERIFY(fields.doseG.isEmpty());
        QVERIFY(fields.yieldG.isEmpty());
        QVERIFY(fields.durationSec.isEmpty());
        QVERIFY(fields.score.isEmpty());
        QVERIFY(fields.notes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_AIManager)

#include "tst_aimanager.moc"
