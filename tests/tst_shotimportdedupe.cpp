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
#include <QSqlDatabase>
#include <QVariantList>
#include <QRegularExpression>

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

    // A failed overwrite import must leave the shot it was replacing intact.
    //
    // The dedupe probes DELETE the existing shot when overwriteExisting is set.
    // Those deletes used to run before any transaction existed, so they
    // autocommitted and ANY later failure destroyed the user's shot and wrote
    // nothing back. The deletes now happen inside the transaction, so they roll
    // back with it.
    //
    // The failure is injected with a BEFORE INSERT trigger rather than lock
    // contention, deliberately: a contending BEGIN IMMEDIATE held across the call
    // would also block the old autocommit DELETE, so the row would survive under
    // the BUGGY code too and the test would pass against the bug it is meant to
    // catch. RAISE(ABORT) unwinds only the failing statement, leaves the
    // transaction open for the rollback to undo, and needs no interleaving.
    void failed_overwrite_import_keeps_the_original_shot()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("import_overwrite_rollback.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));

        const qint64 t = 1753000000;
        const qint64 originalId =
            storage.importShotRecord(makeShot("keep-me", t, "Profile A", QStringLiteral("VIZ-KEEP")), false);
        QVERIFY2(originalId > 0, "seed import should insert");

        // Make every subsequent INSERT into shots fail, deterministically.
        withTempDb(path, "shs_test_trigger", [](QSqlDatabase& db) {
            QSqlQuery(db).exec(QStringLiteral(
                "CREATE TRIGGER fail_shot_insert BEFORE INSERT ON shots "
                "BEGIN SELECT RAISE(ABORT, 'injected import failure'); END"));
        });

        // Same uuid and visualizer id, so the probes match originalId and, with
        // overwriteExisting, mark it for deletion. The replacement INSERT then fails.
        // The injected failure is the point of the test, so let its warning through
        // failOnWarning() — and by consuming it, assert it actually happened.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(QStringLiteral("Failed to import shot.*injected import failure")));
        const qint64 failed =
            storage.importShotRecord(makeShot("keep-me", t, "Profile A", QStringLiteral("VIZ-KEEP")), true);
        QCOMPARE(failed, qint64(-1));

        storage.close();
        drain();

        // The caller was told the import failed. The shot it was replacing must
        // still be there — if the delete escaped the transaction, this is 0.
        int shots = -1;
        withTempDb(path, "shs_test_check", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM shots WHERE id = ?"));
            q.bindValue(0, originalId);
            if (q.exec() && q.next())
                shots = q.value(0).toInt();
        });
        QVERIFY2(shots == 1, "the import failed but destroyed the shot it was replacing");
    }

    // The read-only half of the same ordering: a duplicate we are NOT overwriting
    // must be reported as skipped (0), not failed (-1). Probing inside the
    // transaction made this return -1 whenever the write lock was contended,
    // which both importers count as a failure in their user-facing tally.
    void duplicate_skip_needs_no_write_lock()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("import_skip_no_lock.db");
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(path));

        const qint64 t = 1754000000;
        QVERIFY(storage.importShotRecord(makeShot("dupe", t, "Profile A", QStringLiteral("VIZ-D")), false) > 0);

        // Hold the write lock on another connection for the whole call. Scoped so
        // the QSqlDatabase copy is destroyed before removeDatabase, which warns
        // (and fails the test under failOnWarning) if a handle is still alive.
        {
            QSqlDatabase blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                            QStringLiteral("shs_test_blocker"));
            blocker.setDatabaseName(path);
            QVERIFY(blocker.open());
            QVERIFY(QSqlQuery(blocker).exec(QStringLiteral("BEGIN IMMEDIATE")));

            // Read-only dedupe: this must not need the lock at all.
            QCOMPARE(storage.importShotRecord(makeShot("dupe", t, "Profile A", QStringLiteral("VIZ-D")), false),
                     qint64(0));

            QVERIFY(QSqlQuery(blocker).exec(QStringLiteral("ROLLBACK")));
            blocker.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("shs_test_blocker"));

        // Per the note on drain() above: close and drain before the storage
        // destructs, or the distinct-cache thread can outlive it.
        storage.close();
        drain();
    }

    // ---- merge-integrity guards + shot id map (fix-restore-id-remap) --------
    //
    // A field restore logged "Found 0 existing shots" against a database the app
    // had opened with 1058 seconds earlier. The de-duplication pre-read's result
    // was discarded, so a failed query and an empty table produced the same
    // empty set — and the cost of confusing them is a second copy of the whole
    // history. These lock the refusals in, and the id map that lets stored
    // references follow the renumbering.

    // Builds a source database with `count` shots, returns its path.
    QString makeSourceDb(const QString& name, int count, qint64 baseTs)
    {
        const QString path = m_dir.filePath(name);
        ShotHistoryStorage src;
        [&] { QVERIFY(src.initialize(path)); }();
        for (int i = 0; i < count; i++) {
            src.importShotRecord(makeShot(QStringLiteral("src-uuid-%1").arg(i),
                                          baseTs + i * 600,
                                          QStringLiteral("Src Profile %1").arg(i),
                                          QString()), false);
        }
        src.close();
        drain();
        return path;
    }

    static int countShots(const QString& dbPath)
    {
        return ShotHistoryStorage::getShotCountStatic(dbPath);
    }

    // Read straight from the file. The id-preservation assertions are about
    // what SQLite actually holds, so they must not go through the same import
    // code they are checking.
    static QVariantList queryColumn(const QString& dbPath, const QString& sql)
    {
        QVariantList out;
        const QString conn = QStringLiteral("tst_keepid_%1").arg(reinterpret_cast<quintptr>(&out));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(sql))
                    while (q.next()) out << q.value(0);
            }
        }
        QSqlDatabase::removeDatabase(conn);
        return out;
    }

    static QList<qint64> shotIds(const QString& dbPath)
    {
        QList<qint64> ids;
        for (const QVariant& v : queryColumn(dbPath, QStringLiteral("SELECT id FROM shots ORDER BY id")))
            ids << v.toLongLong();
        return ids;
    }

    static qint64 sequenceFor(const QString& dbPath)
    {
        const QVariantList v = queryColumn(
            dbPath, QStringLiteral("SELECT seq FROM sqlite_sequence WHERE name='shots'"));
        return v.isEmpty() ? -1 : v.first().toLongLong();
    }

    static qint64 shotIdForUuid(const QString& dbPath, const QString& uuid)
    {
        const QVariantList v = queryColumn(
            dbPath, QStringLiteral("SELECT id FROM shots WHERE uuid='%1'").arg(uuid));
        return v.isEmpty() ? -1 : v.first().toLongLong();
    }

    // A genuinely empty destination is not the refused case — it must import.
    void merge_into_empty_destination_imports()
    {
        QVERIFY(m_dir.isValid());
        const QString srcPath = makeSourceDb("mi_src_empty.db", 3, 1760000000);

        const QString destPath = m_dir.filePath("mi_dest_empty.db");
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            dest.close();
            drain();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, true, &r));
        QCOMPARE(r.destShotsBefore.value_or(-1), 0);
        QCOMPARE(r.imported, 3);
        QCOMPARE(r.skipped, 0);
        QVERIFY(r.integrityFailure.isEmpty());
        QCOMPARE(countShots(destPath), 3);
    }

    // A populated destination whose pre-read agrees with the count: the normal
    // path, which must proceed and skip the duplicates rather than doubling.
    void merge_with_agreeing_counts_proceeds_and_skips_duplicates()
    {
        QVERIFY(m_dir.isValid());
        const QString srcPath = makeSourceDb("mi_src_agree.db", 3, 1761000000);

        const QString destPath = m_dir.filePath("mi_dest_agree.db");
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            // Same uuids as the source's first two -> they must be SKIPPED.
            dest.importShotRecord(makeShot("src-uuid-0", 1761000000, "Dest A", QString()), false);
            dest.importShotRecord(makeShot("src-uuid-1", 1761000600, "Dest B", QString()), false);
            dest.close();
            drain();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, true, &r));
        QCOMPARE(r.destShotsBefore.value_or(-1), 2);
        QCOMPARE(r.skipped, 2);
        QCOMPARE(r.imported, 1);
        QVERIFY(r.integrityFailure.isEmpty());
        // The history was merged, not doubled.
        QCOMPARE(countShots(destPath), 3);
    }

    // Every source shot maps to a destination id: inserted rows to their new id,
    // duplicates to the id of the row already present. Nothing maps to itself by
    // accident, which is what makes the renumbering visible to callers.
    void import_reports_a_shot_id_map_covering_inserts_and_skips()
    {
        QVERIFY(m_dir.isValid());
        const QString srcPath = makeSourceDb("mi_src_map.db", 3, 1762000000);

        const QString destPath = m_dir.filePath("mi_dest_map.db");
        qint64 dupDestId = 0;
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            // Pad so destination ids cannot coincide with source ids (1,2,3).
            // Distinct profile names AND a wide timestamp gap: importShotRecord
            // treats same-profile shots within 5 s as near-duplicates
            // (shothistorystorage.cpp:4829), which silently collapsed these to a
            // single row when they shared a name and were 1 s apart.
            for (int i = 0; i < 5; i++)
                QVERIFY(dest.importShotRecord(makeShot(QStringLiteral("pad-%1").arg(i),
                                               1762900000 + i * 3600,
                                               QStringLiteral("Pad %1").arg(i), QString()), false) > 0);
            dupDestId = dest.importShotRecord(
                makeShot("src-uuid-1", 1762000600, "Dest Dup", QString()), false);
            QVERIFY(dupDestId > 0);
            dest.close();
            drain();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, true, &r));

        // All three source shots are accounted for.
        QCOMPARE(r.shotIdMap.size(), 3);
        // The duplicate maps to the row the destination already had — a
        // reference to it must resolve, not be cleared.
        QVERIFY(r.shotIdMap.contains(2));
        QCOMPARE(r.shotIdMap.value(2), dupDestId);
        // The inserted ones got NEW ids, past everything already present.
        for (qint64 srcId : {qint64(1), qint64(3)}) {
            QVERIFY(r.shotIdMap.contains(srcId));
            QVERIFY2(r.shotIdMap.value(srcId) > 5,
                     "inserted shot must get a fresh id past the padding");
        }
        // No entry may be an identity: a source id that survives unchanged is a
        // reference that will silently resolve to the wrong shot later, which is
        // the defect as reported (1109 reading as a valid id after renumbering).
        for (auto it = r.shotIdMap.constBegin(); it != r.shotIdMap.constEnd(); ++it)
            QVERIFY2(it.key() != it.value(), "a source id survived the renumbering unchanged");
    }

    // A destination whose shots table cannot answer the pre-read (here: no uuid
    // column — a foreign or malformed database) must ABORT, not treat the empty
    // result as "nothing here yet" and insert the source on top. The old code
    // discarded the query result and would have imported every row.
    void failed_pre_read_aborts_without_writing()
    {
        QVERIFY(m_dir.isValid());
        const QString srcPath = makeSourceDb("mi_src_badread.db", 3, 1765000000);

        // Hand-built destination: a real SQLite file with a shots table that has
        // rows but no uuid column, so `SELECT uuid, id FROM shots` errors.
        const QString destPath = m_dir.filePath("mi_dest_badread.db");
        {
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("shs_badread"));
            d.setDatabaseName(destPath);
            QVERIFY(d.open());
            QVERIFY(QSqlQuery(d).exec(QStringLiteral(
                "CREATE TABLE shots (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER)")));
            QVERIFY(QSqlQuery(d).exec(QStringLiteral("INSERT INTO shots (timestamp) VALUES (1)")));
            d.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("shs_badread"));

        // init() sets failOnWarning(), and refusing loudly is the point here —
        // so the expected warnings are declared rather than suppressed globally.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("Aborting - .*could not run")));

        ShotHistoryStorage::ImportResult r;
        const bool ok = ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, true, &r);
        QVERIFY2(!ok, "an unanswerable pre-read must fail the import, not proceed");
        QVERIFY2(!r.integrityFailure.isEmpty(), "the refusal must say why");
        QCOMPARE(r.imported, 0);
        // The destination is untouched: still the one row it started with.
        QCOMPARE(countShots(destPath), 1);
        QVERIFY(r.refused());
        // A refused import hands back no map, so idMapOrNull() says "clear every
        // stored id" and every caller agrees without needing its own invariant.
        //
        // Honest limit: this does NOT exercise the `if (!result) shotIdMap.clear()`
        // in the producer. Both guards fire before the first INSERT, so the map
        // is empty here whatever that line does — mutating it away leaves this
        // slot green. The state it actually guards is a replace-mode INSERT
        // failure PART-WAY through, which rolls back with the map already
        // populated; reaching that needs a row to succeed and a later one to
        // fail, which no honest fixture produces. The line stays (it is one
        // statement and it makes the three callers correct by construction) and
        // is recorded as untested rather than covered by this assertion.
        QVERIFY(r.shotIdMap.isEmpty());
        QVERIFY(r.idMapOrNull() == nullptr);
    }

    // Replace mode DELETEs first, so an empty pre-read is expected there and
    // must NOT trip the merge guard.
    void replace_mode_is_not_subject_to_the_merge_guard()
    {
        QVERIFY(m_dir.isValid());
        const QString srcPath = makeSourceDb("mi_src_replace.db", 2, 1764000000);

        const QString destPath = m_dir.filePath("mi_dest_replace.db");
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            for (int i = 0; i < 4; i++)
                QVERIFY(dest.importShotRecord(makeShot(QStringLiteral("r-%1").arg(i),
                                               1764900000 + i * 3600,
                                               QStringLiteral("R %1").arg(i), QString()), false) > 0);
            dest.close();
            drain();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/false, &r));
        QVERIFY(r.integrityFailure.isEmpty());
        QVERIFY(!r.refused());
        QCOMPARE(countShots(destPath), 2);   // replaced, not merged
        // Replace mode never measures the destination, and "not measured" must
        // stay distinguishable from "the destination was empty".
        QVERIFY(!r.destShotsBefore.has_value());
    }

    // A restore must not renumber. Two halves of one rule:
    //   replace — the database comes back with the ids it was backed up with;
    //   merge   — existing rows stay put, incoming rows keep their own id.
    //
    // Before this, the INSERT omitted `id` entirely, so AUTOINCREMENT renumbered
    // every imported row. Replace mode clears with DELETE, which does not reset
    // sqlite_sequence, so restoring a backup into the database it came from
    // moved every shot to a fresh id block — and a renumbered shot is exactly
    // what makes an outside reference to it go stale.
    void replace_restores_the_original_ids_and_the_sequence()
    {
        QVERIFY(m_dir.isValid());
        const QString srcPath = makeSourceDb("mi_src_keepid.db", 3, 1766000000);

        // Give the destination MORE shots than the source, so its sequence sits
        // above anything the source can offer: renumbering would be obvious.
        const QString destPath = m_dir.filePath("mi_dest_keepid.db");
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            for (int i = 0; i < 9; i++)
                QVERIFY(dest.importShotRecord(makeShot(QStringLiteral("k-%1").arg(i),
                                               1766900000 + i * 3600,
                                               QStringLiteral("K %1").arg(i), QString()), false) > 0);
            dest.close();
            drain();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/false, &r));
        QCOMPARE(r.imported, 3);

        // The source's ids are 1..3 and they come back as 1..3, not 10..12.
        QCOMPARE(shotIds(destPath), (QList<qint64>{1, 2, 3}));
        for (auto it = r.shotIdMap.constBegin(); it != r.shotIdMap.constEnd(); ++it)
            QCOMPARE(it.key(), it.value());   // identity: nothing was renumbered

        // And the sequence matches, so the next new shot continues the restored
        // history instead of skipping past the pre-restore high-water mark.
        QCOMPARE(sequenceFor(destPath), qint64(3));
    }

    void merge_keeps_incoming_ids_that_are_free_and_only_moves_collisions()
    {
        QVERIFY(m_dir.isValid());
        // Source ids 1..3.
        const QString srcPath = makeSourceDb("mi_src_freeid.db", 3, 1767000000);

        // Destination occupies id 1 and 2 with DIFFERENT shots, leaving 3 free.
        const QString destPath = m_dir.filePath("mi_dest_freeid.db");
        {
            ShotHistoryStorage dest;
            QVERIFY(dest.initialize(destPath));
            for (int i = 0; i < 2; i++)
                QVERIFY(dest.importShotRecord(makeShot(QStringLiteral("occupied-%1").arg(i),
                                               1767900000 + i * 3600,
                                               QStringLiteral("Occ %1").arg(i), QString()), false) > 0);
            dest.close();
            drain();
        }

        ShotHistoryStorage::ImportResult r;
        QVERIFY(ShotHistoryStorage::importDatabaseStatic(destPath, srcPath, /*merge=*/true, &r));
        QCOMPARE(r.imported, 3);

        // The existing two are exactly where they were.
        QCOMPARE(shotIdForUuid(destPath, QStringLiteral("occupied-0")), qint64(1));
        QCOMPARE(shotIdForUuid(destPath, QStringLiteral("occupied-1")), qint64(2));

        // Source id 3 was free, so that shot kept it. 1 and 2 were taken, so
        // those two moved — and the map is what records where they went.
        QCOMPARE(r.shotIdMap.value(3), qint64(3));
        // The two collisions land past every id either side uses. The specific
        // values are not the contract; not landing on 3 is — an earlier
        // relocation must not consume an id a later source row still owns,
        // which is what letting AUTOINCREMENT choose did.
        QVERIFY2(r.shotIdMap.value(1) > 3, "a relocated id must not take a free source id");
        QVERIFY2(r.shotIdMap.value(2) > 3, "a relocated id must not take a free source id");
        const QList<qint64> dest = r.shotIdMap.values();
        QCOMPARE(QSet<qint64>(dest.begin(), dest.end()).size(), qsizetype(3));  // no two shots share an id
        QCOMPARE(countShots(destPath), 5);
    }

    // existingShotIds is what decides whether a stored conversation reference
    // still resolves, and the caller DELETES what does not. Two shapes matter:
    // it must answer exactly, and it must refuse to answer rather than answer
    // "nothing" when it cannot look.
    void existing_shot_ids_answers_exactly_and_refuses_when_it_cannot_look()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("existing_ids.db");

        qint64 a = 0, c = 0;
        {
            ShotHistoryStorage storage;
            QVERIFY(storage.initialize(path));
            a = storage.importShotRecord(makeShot("e-a", 1765000000, "E A", QString()), false);
            const qint64 b = storage.importShotRecord(makeShot("e-b", 1765003600, "E B", QString()), false);
            c = storage.importShotRecord(makeShot("e-c", 1765007200, "E C", QString()), false);
            QVERIFY(a > 0 && b > 0 && c > 0);

            // The query builds its own `IN (?,?,?)` placeholder list, so a
            // bind-order or placeholder-count regression is a live risk and
            // would show up as the wrong subset here.
            const auto found = storage.existingShotIds({a, c, 999999});
            QVERIFY(found.has_value());
            QCOMPARE(*found, QSet<qint64>({a, c}));

            // An empty request is a real answer ("none of nothing"), not a
            // refusal — the caller must not be pushed down the "leave it alone"
            // path for a conversation that references no shots.
            const auto none = storage.existingShotIds({});
            QVERIFY(none.has_value());
            QVERIFY(none->isEmpty());

            storage.close();
            drain();
        }

        // Not initialized: nullopt, NOT an empty set. An empty set here would
        // tell AIConversation::loadFromStorage that every reference is dead and
        // it would delete them all — the failure mode this return type exists
        // to make unrepresentable.
        ShotHistoryStorage notReady;
        const auto unanswerable = notReady.existingShotIds({a, c});
        QVERIFY(!unanswerable.has_value());
    }
};

QTEST_GUILESS_MAIN(TstShotImportDedupe)
#include "tst_shotimportdedupe.moc"
