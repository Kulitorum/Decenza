#include <QtTest>

#include "ble/blemanager.h"

// BLEManager::ScaleSkipHighLatch — the in-memory dual-HIGH skip-HIGH latch
// value type (#1093/#1176, D7 review hardening). Pure: set()/clear() enforce
// the invariant "triggerKind non-empty AND setTime valid IFF latched", so the
// three correlated fields cannot drift. No BLEManager construction needed —
// the struct is header-inline and Qt-Core-only.

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
};

QTEST_GUILESS_MAIN(tst_ScaleSkipHighLatch)
#include "tst_scaleskiphighlatch.moc"
