#include <QtTest>

#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/dbutils.h"

// beginImmediateTransaction() — the read-then-write lock discipline shared by the
// recipe update, the equipment identity edit, the shot import and the legacy bag
// import. All four sit on the same rule, and before it existed the recipe one lost
// real saves ("database is locked", twice in a three-day field log).
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
    void startedHoldsTheWriteLockNotAReadLock();
    void lockedAfterEveryAttemptAndLeavesNoTransactionBehind();
    void nestedIsRecognisedAndNotRetried();
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
             "(QSQLiteDriver::beginTransaction), beginImmediateTransaction is redundant");
    QVERIFY2(isSqliteLockError(w.lastError()),
             qPrintable(QStringLiteral("upgrade failed for a non-lock reason: %1 (native %2)")
                            .arg(w.lastError().text(), w.lastError().nativeErrorCode())));
    a.rollback();

    // (b) The helper, same interleaving: we hold the write lock from the start,
    // so the other connection is the one that loses and our read-then-write lands.
    QCOMPARE(beginImmediateTransaction(a, "test", 1), TxnBegin::Started);
    QSqlQuery read2(a);
    QVERIFY(read2.exec("SELECT v FROM t WHERE id = 1"));
    QVERIFY(read2.next());
    read2.finish();
    QVERIFY(!run(b, "INSERT INTO t(v) VALUES('interleaved2')"));
    QVERIFY(run(a, "UPDATE t SET v = 'mine' WHERE id = 1"));
    QVERIFY(a.commit());
}

void tst_DbTxn::startedHoldsTheWriteLockNotAReadLock()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    QCOMPARE(beginImmediateTransaction(a, "test"), TxnBegin::Started);
    QVERIFY2(!run(b, "INSERT INTO t(v) VALUES('x')"),
             "another connection wrote while we held the transaction - the BEGIN was not IMMEDIATE");
    QVERIFY(a.rollback());
    QVERIFY(run(b, "INSERT INTO t(v) VALUES('x')"));   // released on rollback
}

void tst_DbTxn::lockedAfterEveryAttemptAndLeavesNoTransactionBehind()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    QVERIFY(run(b, "BEGIN IMMEDIATE"));                // held until released below
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("could not take the write lock after 2 attempts")));
    QCOMPARE(beginImmediateTransaction(a, "test", 2), TxnBegin::Locked);

    // Giving up must not leave a half-open transaction on our connection: the
    // caller is about to return and the connection goes back to the pool.
    QVERIFY(run(b, "ROLLBACK"));
    QCOMPARE(beginImmediateTransaction(a, "test", 1), TxnBegin::Started);
    QVERIFY(a.rollback());
}

void tst_DbTxn::nestedIsRecognisedAndNotRetried()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));

    // Nested is classified by matching SQLite's English message, since it carries
    // no distinct result code. This is the most fragile classification in the
    // helper and the cheapest to pin: if SQLite ever reworded it, this fails
    // rather than silently degrading to Failed at every call site.
    QVERIFY(a.transaction());
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("is nested inside a transaction owned by an outer frame")));
    QCOMPARE(beginImmediateTransaction(a, "test"), TxnBegin::Nested);
    QVERIFY(a.rollback());
}

void tst_DbTxn::activeStatementBlocksTheBeginUntilFinished()
{
    seed();
    QSqlDatabase a = open(QStringLiteral("a"));
    QSqlDatabase b = open(QStringLiteral("b"));

    // The helper's documented precondition. An unfinished SELECT holds a read
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
    QCOMPARE(beginImmediateTransaction(a, "test", 1), TxnBegin::Locked);

    probe.finish();
    QCOMPARE(beginImmediateTransaction(a, "test", 1), TxnBegin::Started);
    QVERIFY(a.rollback());
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
