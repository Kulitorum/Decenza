// Regression test for the Visualizer-recovery dedupe key in
// ShotHistoryStorage::importShotRecord (via importShotRecordStatic).
//
// The crux: a shot that was pulled ON THIS DEVICE has a filename-keyed local
// uuid, while the recovery import derives its uuid from the Visualizer id — so
// the two never match. Recovered shots therefore fell through to the weaker
// timestamp + profile_name near-duplicate check, which a shot with a
// differently-formatted or later-renamed profile title slips past, re-importing
// as a duplicate row. The visualizer_id probe fixes that by matching on the one
// identifier guaranteed identical on both sides (the local shot's visualizer_id
// column, set when it was uploaded). These tests lock that in.

#include <QtTest>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QThread>
#include <QPointF>
#include <QSqlQuery>

#include "history/shothistorystorage.h"
#include "history/shothistory_types.h"
#include "core/dbutils.h"

class TstShotImportDedupe : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    // initialize() spawns a distinct-cache thread; close() + drain before the
    // storage destructs or it can crash (see tst_coffeebags::initAndClose).
    static void drain()
    {
        for (int i = 0; i < 20; i++) {
            QCoreApplication::processEvents();
            QThread::msleep(25);
        }
    }

    static ShotRecord makeShot(const QString& uuid, qint64 ts,
                               const QString& profileName, const QString& vizId)
    {
        ShotRecord r;
        r.summary.uuid = uuid;
        r.summary.timestamp = ts;
        r.summary.profileName = profileName;
        r.summary.beverageType = "espresso";
        r.visualizerId = vizId;
        r.pressure.append(QPointF(0.0, 6.0));   // one sample so it's a real shot
        return r;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void recovery_dedupes_by_visualizer_id()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("import_dedupe.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));

        const qint64 t = 1751000000;

        // A shot pulled on this device and later uploaded: filename-keyed uuid,
        // a title, and its visualizer_id set.
        const qint64 localId =
            storage.importShotRecord(makeShot("local-filename-uuid", t, "Profile A", "VIZ-123"), false);
        QVERIFY2(localId > 0, "first import should insert");

        // The same shot returning via recovery: DIFFERENT uuid AND a DIFFERENT
        // profile title (e.g. a de1app-formatted title), but SAME visualizer id +
        // timestamp. Must be recognised as a duplicate and skipped — this is the
        // exact case the visualizer_id probe exists for; the uuid and
        // timestamp+profile_name checks both miss it.
        const qint64 dupId =
            storage.importShotRecord(makeShot("viz-keyed-uuid", t, "profile a (de1app)", "VIZ-123"), false);
        QCOMPARE(dupId, qint64(0));

        // A genuinely different Visualizer shot still imports.
        const qint64 otherId =
            storage.importShotRecord(makeShot("other-uuid", t + 7200, "Profile B", "VIZ-999"), false);
        QVERIFY2(otherId > 0, "distinct visualizer id should insert");

        storage.close();
        drain();
    }

    // An empty visualizer id is not an identity: the probe is guarded by a
    // non-empty check, so two unrelated shots with no visualizer id (the .shot
    // import path) must both insert rather than "" matching "".
    void empty_visualizer_id_does_not_false_match()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("import_dedupe_empty.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));

        const qint64 t = 1751500000;
        QVERIFY(storage.importShotRecord(makeShot("uuid-1", t,       "Profile One", QString()), false) > 0);
        QVERIFY(storage.importShotRecord(makeShot("uuid-2", t + 100, "Profile Two", QString()), false) > 0);

        storage.close();
        drain();
    }

    // Phase markers must survive import. shot_phases.label is NOT NULL, so a
    // marker with a null/empty label makes its INSERT fail — and the import
    // ignores that failure, silently dropping every frame line. A recovered
    // shot's markers (built in parseVisualizerShot) must carry a label; this
    // verifies the rows actually land in the DB.
    void phase_markers_persist()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("import_phases.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));

        ShotRecord r = makeShot("phase-shot", 1752000000, "Profile P", QStringLiteral("VIZ-P"));
        HistoryPhaseMarker m0; m0.time = 0.01; m0.frameNumber = 0; m0.label = QStringLiteral("Frame 1");
        HistoryPhaseMarker m1; m1.time = 2.0;  m1.frameNumber = 1; m1.label = QStringLiteral("Frame 2");
        r.phases = { m0, m1 };

        const qint64 id = storage.importShotRecord(r, false);
        QVERIFY2(id > 0, "import should insert");

        storage.close();
        drain();

        // A null-label marker would have failed the NOT NULL constraint and been
        // silently skipped, leaving 0 rows.
        int phaseCount = -1;
        withTempDb(path, "shs_test_phases", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM shot_phases WHERE shot_id = ?"));
            q.bindValue(0, id);
            if (q.exec() && q.next())
                phaseCount = q.value(0).toInt();
        });
        QCOMPARE(phaseCount, 2);
    }
};

QTEST_GUILESS_MAIN(TstShotImportDedupe)
#include "tst_shotimportdedupe.moc"
