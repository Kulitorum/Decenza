#include <QtTest>

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/dbutils.h"

// DbWriteTxn — the read-then-write lock discipline shared by the recipe update,
// the equipment identity edit, the equipment delete, the shot import, the import
// backfill and the legacy bag import. They all sit on the same rule, and before it
// existed the recipe one lost real saves ("database is locked", twice in a
// three-day field log).
//
// Every case here is deterministic, despite testing lock contention. Two things
// make that work:
//
//  - The bug is NOT a race. SQLite refuses the read→write upgrade the instant it
//    sees a stale snapshot, without consulting busy_timeout, so reproducing it
//    needs two statements in a fixed order on one thread — no interleaving hook,
//    no window to miss.
//  - The contending connection holds its lock for the whole test function and is
//    released explicitly, never on a timer.
//
// So there are no qWait()s, no elapsed-time assertions, and nothing that degrades
// under `ctest -j`.
class tst_DbTxn : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void cleanup();

    void deferredBeginLosesTheUpgradeThatImmediateSurvives();
    void okHoldsTheWriteLockNotAReadLock();
    void scopeExitWithoutCommitRollsBack();
    void deleteBeforeTheTransactionIsNotUndone();
    void commitReleasesSoTheDestructorIsInert();
    void lockedAfterEveryAttemptAndLeavesNoTransactionBehind();
    void nestedIsRefusedNotAdopted();
    void activeStatementBlocksTheBeginUntilFinished();
    void constraintFailureIsNotMistakenForALock();

private:
    QTemporaryDir m_dir;
    int m_seq = 0;
    QStringList m_conns;

    // Raw connections rather than withTempDb: its busy_timeout is 5000 ms, which
    // would make the contended cases take ~10 s apiece for no added coverage.
    QSqlDatabase open(const QString& name) {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(m_path);
        if (!db.open())
            return db;
        QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout = 20"));
        QSqlQuery(db).exec(QStringLiteral("PRAGMA journal_mode = WAL"));  // as shots.db
        m_conns << name;
        return db;
    }
    static bool run(QSqlDatabase& db, const char* sql) {
        QSqlQuery q(db);
        return q.exec(QLatin1String(sql));
    }
    // Fresh file per test so a stuck lock cannot leak between them.
    void seed() {
        m_path = m_dir.filePath(QStringLiteral("t%1.db").arg(++m_seq));
        QSqlDatabase db = open(QStringLiteral("seed%1").arg(m_seq));
        QVERIFY(db.isOpen());
        QVERIFY(run(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT UNIQUE)"));
        QVERIFY(run(db, "INSERT INTO t(id, v) VALUES(1, 'seed')"));
    }
    QString m_path;
};

void tst_DbTxn::cleanup()
{
    // Connections must be closed before removeDatabase, and every test's
    // transactions must be terminated or the next open would inherit the lock.
    for (const QString& name : std::as_const(m_conns)) {
        {
            QSqlDatabase db = QSqlDatabase::database(name, false);
            if (db.isOpen()) {
                db.rollback();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
    }
    m_conns.clear();
}

// The bug and the fix, under one identical interleaving.
void tst_DbTxn::deferredBeginLosesTheUpgradeThatImmediateSurvives()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    // (a) Qt's plain BEGIN — what every one of these sites used to do.
    QVERIFY(a.transaction());
    QSqlQuery read(a);
    QVERIFY(read.exec("SELECT v FROM t WHERE id = 1"));
    QVERIFY(read.next());                                   // snapshot taken here
    read.finish();
    QVERIFY(run(b, "INSERT INTO t(v) VALUES('interleaved')"));  // someone else commits
    QSqlQuery w(a);
    QVERIFY2(!w.exec("UPDATE t SET v = 'mine' WHERE id = 1"),
             "the DEFERRED upgrade succeeded - if Qt's driver now issues BEGIN IMMEDIATE "
             "(QSQLiteDriver::beginTransaction), DbWriteTxn is redundant");
    QVERIFY2(isSqliteLockError(w.lastError()),
             qPrintable(QStringLiteral("upgrade failed for a non-lock reason: %1 (native %2)")
                            .arg(w.lastError().text(), w.lastError().nativeErrorCode())));
    a.rollback();

    // (b) The guard, same interleaving: we hold the write lock from the start,
    // so the other connection is the one that loses and our read-then-write lands.
    DbWriteTxn txn = DbWriteTxn::begin(a, "test", 1);
    QVERIFY(txn.ok());
    QSqlQuery read2(a);
    QVERIFY(read2.exec("SELECT v FROM t WHERE id = 1"));
    QVERIFY(read2.next());
    read2.finish();
    QVERIFY(!run(b, "INSERT INTO t(v) VALUES('interleaved2')"));
    QVERIFY(run(a, "UPDATE t SET v = 'mine' WHERE id = 1"));
    QVERIFY(txn.commit());
}

void tst_DbTxn::okHoldsTheWriteLockNotAReadLock()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    {
        DbWriteTxn txn = DbWriteTxn::begin(a, "test");
        QVERIFY(txn.ok());
        QVERIFY2(!run(b, "INSERT INTO t(v) VALUES('x')"),
                 "another connection wrote while we held the transaction - the BEGIN was not IMMEDIATE");
    }
    QVERIFY(run(b, "INSERT INTO t(v) VALUES('x')"));   // released when the guard died
}

// The property the enum could not enforce, and the whole reason for the guard: an
// early return out of the middle of a transaction must undo the partial work.
// Every rejection path in RecipeStorage::requestUpdateRecipe and every `return -1`
// in EquipmentStorage::supersedeOrEditStatic now depends on exactly this.
void tst_DbTxn::scopeExitWithoutCommitRollsBack()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));

    {
        DbWriteTxn txn = DbWriteTxn::begin(a, "test");
        QVERIFY(txn.ok());
        QVERIFY(run(a, "UPDATE t SET v = 'half-applied' WHERE id = 1"));
        // ...and the caller bails without committing, as a validation failure does.
    }

    QSqlQuery check(a);
    QVERIFY(check.exec("SELECT v FROM t WHERE id = 1"));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QStringLiteral("seed"));
}

// A DELETE issued before the transaction opens is already committed and cannot
// be taken back — which is how the shot importer lost shots: its dedupe probes
// deleted the row they were replacing in autocommit, and a later failure left
// nothing in its place. This pins the ordering that fixes it: with the delete
// INSIDE the transaction, abandoning the work restores the row.
void tst_DbTxn::deleteBeforeTheTransactionIsNotUndone()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));

    // The old shape: delete first, then open the transaction and fail.
    QVERIFY(run(a, "DELETE FROM t WHERE id = 1"));
    {
        DbWriteTxn txn = DbWriteTxn::begin(a, "test");
        QVERIFY(txn.ok());
        // caller bails without committing
    }
    QSqlQuery gone(a);
    QVERIFY(gone.exec("SELECT COUNT(*) FROM t WHERE id = 1"));
    QVERIFY(gone.next());
    QCOMPARE(gone.value(0).toInt(), 0);   // the row is gone for good
    gone.finish();

    // The fixed shape: transaction first, so the delete rolls back with it.
    QVERIFY(run(a, "INSERT INTO t(id, v) VALUES(1, 'seed')"));
    {
        DbWriteTxn txn = DbWriteTxn::begin(a, "test");
        QVERIFY(txn.ok());
        QVERIFY(run(a, "DELETE FROM t WHERE id = 1"));
        // caller bails without committing
    }
    QSqlQuery restored(a);
    QVERIFY(restored.exec("SELECT v FROM t WHERE id = 1"));
    QVERIFY2(restored.next(), "the row was deleted inside a transaction that never committed, "
                              "but did not come back");
    QCOMPARE(restored.value(0).toString(), QStringLiteral("seed"));
}

void tst_DbTxn::commitReleasesSoTheDestructorIsInert()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));

    {
        DbWriteTxn txn = DbWriteTxn::begin(a, "test");
        QVERIFY(txn.ok());
        QVERIFY(run(a, "UPDATE t SET v = 'kept' WHERE id = 1"));
        QVERIFY(txn.commit());
        // A destructor that rolled back regardless would undo the commit — or
        // worse, roll back whatever transaction came next on this connection.
    }

    QSqlQuery check(a);
    QVERIFY(check.exec("SELECT v FROM t WHERE id = 1"));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QStringLiteral("kept"));
}

void tst_DbTxn::lockedAfterEveryAttemptAndLeavesNoTransactionBehind()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    QVERIFY(run(b, "BEGIN IMMEDIATE"));                // held until released below
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("could not take the write lock after 2 attempts")));
    DbWriteTxn locked = DbWriteTxn::begin(a, "test", 2);
    QVERIFY(!locked.ok());
    // lockTimedOut() is what RecipeStorage turns into the actionable "the database
    // was busy - tap Save again" instead of the generic failure string.
    QVERIFY(locked.lockTimedOut());

    // Giving up must not leave a half-open transaction on our connection: the
    // caller is about to return and the connection goes back to the pool.
    QVERIFY(run(b, "ROLLBACK"));
    DbWriteTxn retry = DbWriteTxn::begin(a, "test", 1);
    QVERIFY(retry.ok());
}

void tst_DbTxn::nestedIsRefusedNotAdopted()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));

    // A nested begin is a failure, not something to adopt: adopting would let an
    // inner rejection path leave its partial writes staged in the outer caller's
    // transaction. It is recognised by matching SQLite's English message, since it
    // carries no distinct result code — the most fragile classification here and
    // the cheapest to pin. lockTimedOut() must stay false, so no caller tells the
    // user to "try again" for something retrying cannot fix.
    QVERIFY(a.transaction());
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("is nested inside a transaction owned by an outer frame")));
    DbWriteTxn nested = DbWriteTxn::begin(a, "test");
    QVERIFY(!nested.ok());
    QVERIFY(!nested.lockTimedOut());
    // The outer transaction must be untouched — still open, still the caller's.
    QVERIFY(run(a, "UPDATE t SET v = 'outer' WHERE id = 1"));
    QVERIFY(a.rollback());
}

void tst_DbTxn::activeStatementBlocksTheBeginUntilFinished()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    // The guard's documented precondition. An unfinished SELECT holds a read
    // transaction open, and SQLite will not run the busy handler while the
    // connection sits in one — so the BEGIN fails instantly and retrying is
    // futile, however idle the database is. This is not hypothetical: the legacy
    // bag import and the shot import both left a probe statement active, which
    // made the fix a guaranteed no-op there until they called finish().
    QSqlQuery probe(a);
    QVERIFY(probe.exec("SELECT COUNT(*) FROM t"));
    QVERIFY(probe.next());
    QVERIFY(run(b, "INSERT INTO t(v) VALUES('after-the-snapshot')"));

    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("could not take the write lock after 1 attempts")));
    QVERIFY(!DbWriteTxn::begin(a, "test", 1).ok());

    probe.finish();
    DbWriteTxn afterFinish = DbWriteTxn::begin(a, "test", 1);
    QVERIFY(afterFinish.ok());
}

void tst_DbTxn::constraintFailureIsNotMistakenForALock()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));

    // A retryable error and a permanent one must never be confused: retrying a
    // constraint violation would just burn the backoff and report Locked.
    QVERIFY(run(a, "INSERT INTO t(v) VALUES('dup')"));
    QSqlQuery q(a);
    QVERIFY(!q.exec("INSERT INTO t(v) VALUES('dup')"));
    QVERIFY2(!isSqliteLockError(q.lastError()),
             qPrintable(QStringLiteral("a UNIQUE violation was classified as contention: %1")
                            .arg(q.lastError().text())));
}

QTEST_GUILESS_MAIN(tst_DbTxn)
#include "tst_dbtxn.moc"
