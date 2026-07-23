#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>

#include "core/settingsstoremigration.h"

// Covers the one-time DE1Qt -> canonical store migration. Every case runs over
// temp IniFormat stores handed to migrateLegacySettingsStore() directly, so no
// test ever addresses a real store — which matters more than usual here, since
// this is the one migration that destroys its own source.

namespace {
constexpr auto kGuardKey = "migration/settings_store_de1qt_to_decenza_done";
}

class tst_SettingsStoreMigration : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString canonicalPath() const { return m_dir.path() + QStringLiteral("/canonical.ini"); }
    QString legacyPath() const { return m_dir.path() + QStringLiteral("/legacy.ini"); }

    // Fresh handles per call: QSettings caches, and the code under test writes
    // through its own handles.
    QSettings canonical() const { return QSettings(canonicalPath(), QSettings::IniFormat); }
    QSettings legacy() const { return QSettings(legacyPath(), QSettings::IniFormat); }

private slots:
    void init()
    {
        QTest::failOnWarning();
        QVERIFY(m_dir.isValid());
        QFile::remove(canonicalPath());
        QFile::remove(legacyPath());
    }

    void carriesLegacySettingsAcrossAndDestroysTheSource()
    {
        {
            QSettings l = legacy();
            l.setValue("profile/current", "adaptive_v2");
            l.setValue("calibration/flowMultiplier", 1.25);
            l.setValue("knownScales/primaryAddress", "sim:00:00:00:00:00:00");
            l.sync();
        }

        QSettings c = canonical();
        QSettings l = legacy();
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(c, l);

        QCOMPARE(r.legacyKeyCount, 3);
        QCOMPARE(r.copied, 3);
        QVERIFY(!r.deferredOnError);
        QVERIFY(r.guardStamped);
        QVERIFY(r.legacyDestroyed);

        QSettings after = canonical();
        QCOMPARE(after.value("profile/current").toString(), QStringLiteral("adaptive_v2"));
        QCOMPARE(after.value("calibration/flowMultiplier").toDouble(), 1.25);
        QCOMPARE(after.value("knownScales/primaryAddress").toString(),
                 QStringLiteral("sim:00:00:00:00:00:00"));

        // Destroyed, not merely unread.
        QVERIFY(!QFile::exists(legacyPath()));
        QVERIFY(legacy().allKeys().isEmpty());
    }

    void canonicalValueWinsOnCollision()
    {
        {
            QSettings l = legacy();
            l.setValue("profile/current", "old_from_legacy");
            l.setValue("only/inLegacy", "carried");
            l.sync();
            QSettings c = canonical();
            c.setValue("profile/current", "current_build_wrote_this");
            c.sync();
        }

        QSettings c = canonical();
        QSettings l = legacy();
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(c, l);

        QCOMPARE(r.legacyKeyCount, 2);
        QCOMPARE(r.copied, 1);   // the colliding key was skipped, not overwritten

        QSettings after = canonical();
        QCOMPARE(after.value("profile/current").toString(),
                 QStringLiteral("current_build_wrote_this"));
        QCOMPARE(after.value("only/inLegacy").toString(), QStringLiteral("carried"));
    }

    void secondRunIsANoOp()
    {
        {
            QSettings l = legacy();
            l.setValue("profile/current", "adaptive_v2");
            l.sync();
        }
        {
            QSettings c = canonical();
            QSettings l = legacy();
            (void)migrateLegacySettingsStore(c, l);
        }

        // A user changes the setting after migrating.
        {
            QSettings c = canonical();
            c.setValue("profile/current", "changed_after_migration");
            c.sync();
        }

        QSettings c = canonical();
        QSettings l = legacy();
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(c, l);

        QVERIFY(r.alreadyDone);
        QCOMPARE(r.copied, 0);
        QVERIFY(!r.guardStamped);   // not re-stamped; nothing was touched
        QCOMPARE(canonical().value("profile/current").toString(),
                 QStringLiteral("changed_after_migration"));
    }

    void emptyLegacyStoreStampsTheGuardWithoutCopying()
    {
        QSettings c = canonical();
        QSettings l = legacy();
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(c, l);

        QCOMPARE(r.legacyKeyCount, 0);
        QCOMPARE(r.copied, 0);
        QVERIFY(!r.deferredOnError);
        QVERIFY(r.guardStamped);
        QVERIFY(canonical().value(QLatin1String(kGuardKey), false).toBool());
    }

    // Regression: the first real run reported "copied 44 of 118 legacy key(s)" for
    // a store holding 44. QSettings merges the fallback search list into allKeys()
    // and contains(), so 74 unrelated preferences were enumerated as migration
    // candidates; they were skipped only because contains() consulted the same
    // fallbacks on the canonical side. Break that symmetry and foreign keys get
    // copied into Decenza's store. Both handles must speak only for their own store.
    void countsAndCopiesOnlyItsOwnStoresKeys()
    {
        {
            QSettings l = legacy();
            l.setValue("profile/current", "adaptive_v2");
            l.setValue("calibration/flowMultiplier", 1.25);
            l.sync();
        }

        QSettings c = canonical();
        QSettings l = legacy();
        // Fallbacks left at Qt's default (enabled) on the way in — the function
        // under test is responsible for turning them off, not its caller.
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(c, l);

        QCOMPARE(r.legacyKeyCount, 2);
        QCOMPARE(r.copied, 2);

        QSettings after = canonical();
        after.setFallbacksEnabled(false);
        QStringList keys = after.allKeys();
        keys.removeAll(QStringLiteral("migration/settings_store_de1qt_to_decenza_done"));
        std::sort(keys.begin(), keys.end());
        QCOMPARE(keys, (QStringList{QStringLiteral("calibration/flowMultiplier"),
                                    QStringLiteral("profile/current")}));
    }

    // The ordering hazard this migration has to defuse. AccessibilityManager's
    // own one-time migration is guarded by a flag that lived in the legacy store;
    // if it were constructed before this migration ran, it would see an unstamped
    // guard and overwrite the user's CURRENT accessibility settings with values
    // from the long-abandoned ("Decenza", "DE1") store. Carrying the guard across
    // is what makes constructing it afterwards safe — paired with
    // tst_accessibility_announcements' legacyMigration_idempotentWhenGuardSet(),
    // which proves a stamped guard means "do nothing".
    void carriesTheAccessibilityMigrationGuardAcross()
    {
        {
            QSettings l = legacy();
            l.setValue("accessibility/_migratedFromLegacyV1", true);
            l.setValue("accessibility/tickVolume", 42);
            l.sync();
        }

        QSettings c = canonical();
        QSettings l = legacy();
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(c, l);
        QVERIFY(r.guardStamped);

        QSettings after = canonical();
        QVERIFY2(after.value("accessibility/_migratedFromLegacyV1", false).toBool(),
                 "an AccessibilityManager constructed after this migration must see its "
                 "guard as already stamped, or it re-runs and clobbers current settings");
        QCOMPARE(after.value("accessibility/tickVolume").toInt(), 42);
    }

    // The contract that makes destroying the source safe: if the canonical store
    // cannot accept the writes, nothing is destroyed and the guard stays unstamped
    // so the next launch retries.
    void unwritableCanonicalStoreDefersWithoutDestroying()
    {
        {
            QSettings l = legacy();
            l.setValue("profile/current", "adaptive_v2");
            l.sync();
        }

        // A directory where the store file should be: QSettings reports
        // AccessError rather than silently dropping the write.
        const QString blockedPath = m_dir.path() + QStringLiteral("/blocked.ini");
        QVERIFY(QDir().mkpath(blockedPath));

        QSettings blocked(blockedPath, QSettings::IniFormat);
        QSettings l = legacy();
        const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(blocked, l);

        QVERIFY(r.deferredOnError);
        QVERIFY(!r.guardStamped);
        QVERIFY(!r.legacyDestroyed);

        // The source survives intact, which is the whole point.
        QSettings survivor = legacy();
        QCOMPARE(survivor.value("profile/current").toString(), QStringLiteral("adaptive_v2"));
        QVERIFY(QFile::exists(legacyPath()));
    }
};

QTEST_MAIN(tst_SettingsStoreMigration)
#include "tst_settingsstoremigration.moc"
