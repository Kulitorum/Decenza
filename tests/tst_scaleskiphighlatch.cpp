#include <QtTest>
#include <QStandardPaths>

#include "ble/blemanager.h"
#include "core/settings_hardware.h"

// BLEManager::ScaleSkipHighLatch — the in-memory dual-HIGH skip-HIGH latch
// value type (#1093/#1176, D7 review hardening). Pure: set()/clear() enforce
// the invariant "triggerKind non-empty AND setTime valid IFF latched", so the
// three correlated fields cannot drift. No BLEManager construction needed —
// the struct is header-inline and Qt-Core-only.
//
// Also covers the D9 persisted (build-scoped) classification storage on
// SettingsHardware (the dumb store; BLEManager owns the build-code gating).
// QStandardPaths test mode isolates QSettings so the real user settings are
// untouched.

class tst_ScaleSkipHighLatch : public QObject {
    Q_OBJECT

private slots:
    void defaultIsUnlatchedAndEmpty() {
        BLEManager::ScaleSkipHighLatch l;
        QVERIFY(!l.latched);
        QVERIFY(l.triggerKind.isEmpty());
        QVERIFY(!l.setTime.isValid());
    }

    void setEstablishesCorrelatedState() {
        BLEManager::ScaleSkipHighLatch l;
        const QDateTime before = QDateTime::currentDateTime();
        l.set(QStringLiteral("scale-feed-stall"));
        QVERIFY(l.latched);
        QCOMPARE(l.triggerKind, QStringLiteral("scale-feed-stall"));
        QVERIFY(l.setTime.isValid());
        QVERIFY(l.setTime >= before);  // stamped at set() time
    }

    void emptyKindIsSalvagedToUnknownNotBlank() {
        // Belt-and-suspenders: the public BLEManager API mandates a kind, but
        // the value type must never record a latched-but-blank state.
        BLEManager::ScaleSkipHighLatch l;
        l.set(QString());
        QVERIFY(l.latched);
        QCOMPARE(l.triggerKind, QStringLiteral("unknown"));
        QVERIFY(l.setTime.isValid());
    }

    void clearRestoresUnlatchedInvariant() {
        BLEManager::ScaleSkipHighLatch l;
        l.set(QStringLiteral("de1-fault-cluster"));
        l.clear();
        QVERIFY(!l.latched);
        QVERIFY(l.triggerKind.isEmpty());
        QVERIFY(!l.setTime.isValid());
    }

    void reSetOverwritesKindAndTimestamp() {
        BLEManager::ScaleSkipHighLatch l;
        l.set(QStringLiteral("de1-fault-cluster"));
        const QDateTime first = l.setTime;
        QTest::qWait(5);  // ensure a measurable timestamp delta
        l.set(QStringLiteral("scale-feed-stall"));
        QCOMPARE(l.triggerKind, QStringLiteral("scale-feed-stall"));
        QVERIFY(l.setTime >= first);
        QVERIFY(l.latched);
    }

    // rehydrate(): the persistence trust boundary. Unlike set() it preserves
    // the original set-time, and it sanitises corrupt persisted input so the
    // "kind non-empty AND time valid IFF latched" invariant cannot be broken
    // by a partial write / manual edit / ISO drift (review finding A).

    void rehydrateValidPreservesOriginalTime() {
        BLEManager::ScaleSkipHighLatch l;
        const QDateTime original = QDateTime::currentDateTime().addSecs(-3600);
        QVERIFY(l.rehydrate(QStringLiteral("scale-feed-stall"), original));
        QVERIFY(l.latched);
        QCOMPARE(l.triggerKind, QStringLiteral("scale-feed-stall"));
        QCOMPARE(l.setTime, original);  // preserved, NOT re-stamped (vs set())
    }

    void rehydrateEmptyKindBecomesUnknown() {
        BLEManager::ScaleSkipHighLatch l;
        QVERIFY(l.rehydrate(QString(), QDateTime::currentDateTime()));
        QVERIFY(l.latched);
        QCOMPARE(l.triggerKind, QStringLiteral("unknown"));
    }

    void rehydrateInvalidTimeSanitisedInvariantHolds() {
        BLEManager::ScaleSkipHighLatch l;
        // Corrupt/missing persisted timestamp → fromString() yields invalid.
        const bool timeOk =
            l.rehydrate(QStringLiteral("de1-fault-cluster"),
                        QDateTime::fromString(QStringLiteral("garbled"), Qt::ISODate));
        QVERIFY(!timeOk);                 // signals the anomaly for logging
        QVERIFY(l.latched);               // classification kept (load-bearing)
        QCOMPARE(l.triggerKind, QStringLiteral("de1-fault-cluster"));
        QVERIFY(l.setTime.isValid());     // substituted — invariant intact
    }

    // --- D9: SettingsHardware persisted record (build-scoped store) ---

    void initTestCase() {
        // Isolate QSettings so the real user prefs are untouched.
        QStandardPaths::setTestModeEnabled(true);
    }

    void persistedDefaultIsUnlatched() {
        SettingsHardware s;
        s.clearConnectionPriorityLatch();  // ensure clean slate
        QVERIFY(!s.cpLatched());
        QVERIFY(s.cpTriggerKind().isEmpty());
        QVERIFY(s.cpSetTimeIso().isEmpty());
        QCOMPARE(s.cpBuildCode(), 0);
    }

    void persistedRoundTrips() {
        SettingsHardware s;
        const QString iso = QDateTime::currentDateTime().toString(Qt::ISODate);
        s.setConnectionPriorityLatch(QStringLiteral("scale-feed-stall"), iso, 3388);
        QVERIFY(s.cpLatched());
        QCOMPARE(s.cpTriggerKind(), QStringLiteral("scale-feed-stall"));
        QCOMPARE(s.cpSetTimeIso(), iso);
        QCOMPARE(s.cpBuildCode(), 3388);

        // A fresh instance reads the same persisted values (true persistence).
        SettingsHardware s2;
        QVERIFY(s2.cpLatched());
        QCOMPARE(s2.cpBuildCode(), 3388);

        s.clearConnectionPriorityLatch();
        SettingsHardware s3;
        QVERIFY(!s3.cpLatched());
        QVERIFY(s3.cpTriggerKind().isEmpty());
        QCOMPARE(s3.cpBuildCode(), 0);
    }

    void buildScopeComparisonIsTheGate() {
        // BLEManager seeds iff storedBuildCode == versionCode(); this verifies
        // the stored buildCode is faithfully retrievable for that comparison
        // (the gate logic itself lives in BLEManager::setSettings).
        SettingsHardware s;
        s.setConnectionPriorityLatch(QStringLiteral("de1-fault-cluster"),
                                     QStringLiteral("2026-05-18T12:00:00"), 3388);
        QCOMPARE(s.cpBuildCode(), 3388);   // same-build path would seed
        QVERIFY(s.cpBuildCode() != 3389);  // different-build path would wipe
        s.clearConnectionPriorityLatch();
    }

    void isoSetTimeRoundTripsToValidDateTime() {
        // The exact path BLEManager::setSettings() depends on: a timestamp
        // written via toString(Qt::ISODate) must parse back via
        // fromString(Qt::ISODate) to a VALID, equivalent QDateTime. If Qt's
        // ISO write/read ever diverge (platform/locale/tz), setSettings()
        // would silently seed an invalid set-time (review finding / test gap).
        SettingsHardware s;
        const QDateTime before = QDateTime::currentDateTime();
        s.setConnectionPriorityLatch(QStringLiteral("scale-feed-stall"),
                                     before.toString(Qt::ISODate), 3388);

        const QDateTime parsed =
            QDateTime::fromString(s.cpSetTimeIso(), Qt::ISODate);
        QVERIFY2(parsed.isValid(),
                 qPrintable(QStringLiteral("ISO round-trip invalid: stored=\"%1\"")
                                .arg(s.cpSetTimeIso())));
        // toString(Qt::ISODate) drops sub-second precision → allow 1 s.
        QVERIFY(qAbs(parsed.secsTo(before)) <= 1);
        s.clearConnectionPriorityLatch();
    }

    // --- Backoff policy mode persistence (observe-mode change) ---

    void cpModeDefaultsEmptyAndRoundTrips() {
        SettingsHardware s;
        s.clearConnectionPriorityLatch();
        QVERIFY(s.cpMode().isEmpty());          // absent ⇒ caller treats as enforce
        s.setCpMode(QStringLiteral("observe"));
        QCOMPARE(s.cpMode(), QStringLiteral("observe"));
        SettingsHardware s2;                    // simulated restart
        QCOMPARE(s2.cpMode(), QStringLiteral("observe"));
        s.setCpMode(QStringLiteral("enforce"));
        QCOMPARE(SettingsHardware().cpMode(), QStringLiteral("enforce"));
    }

    // The critical correctness fix: clearing the latch (MCP reset /
    // new-build re-detect) must NOT wipe the sibling policyMode key.
    void cpModeSurvivesLatchClear() {
        SettingsHardware s;
        s.setCpMode(QStringLiteral("observe"));
        s.setConnectionPriorityLatch(QStringLiteral("scale-feed-stall"),
                                     QDateTime::currentDateTime().toString(Qt::ISODate),
                                     3388);
        QVERIFY(s.cpLatched());

        s.clearConnectionPriorityLatch();       // narrowed to the 4 latch keys

        QVERIFY(!s.cpLatched());                // latch gone
        QCOMPARE(s.cpMode(), QStringLiteral("observe"));  // mode preserved
        QVERIFY(s.cpTriggerKind().isEmpty());   // no stale latch metadata
        QVERIFY(s.cpSetTimeIso().isEmpty());
        QCOMPARE(s.cpBuildCode(), 0);
        s.setCpMode(QString());                 // tidy for the next test
    }

    void cleanupTestCase() {
        SettingsHardware s;
        s.clearConnectionPriorityLatch();
        s.setCpMode(QString());
    }
};

QTEST_GUILESS_MAIN(tst_ScaleSkipHighLatch)
#include "tst_scaleskiphighlatch.moc"
