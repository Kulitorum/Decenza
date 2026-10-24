#include "remote/backupoperationpolicy.h"
#include "remote/backupservicesource.h"

#include <QtTest>

class tst_BackupOperationPolicy final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void operationsAndArgumentsAreClosed()
    {
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("createBackup")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("restoreBackup")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("setBackupHour")));
        QVERIFY(!BackupOperationPolicy::supported(QStringLiteral("openPath")));
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("createBackup"), {}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("createBackup"),
                                                        {{QStringLiteral("path"), QStringLiteral("/tmp/x")}}));
        const QVariantMap restore{{QStringLiteral("backupId"), QString(64, QLatin1Char('a'))},
                                  {QStringLiteral("merge"), true}, {QStringLiteral("shots"), true},
                                  {QStringLiteral("settings"), true}, {QStringLiteral("profiles"), true},
                                  {QStringLiteral("media"), true}};
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("restoreBackup"), restore));
        auto traversal = restore;
        traversal.insert(QStringLiteral("backupId"), QStringLiteral("../backup.zip"));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("restoreBackup"), traversal));
        auto extra = restore;
        extra.insert(QStringLiteral("filename"), QStringLiteral("shots_backup_20261022.zip"));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("restoreBackup"), extra));
    }

    void backupHourIsBoundedAndAcceptsQmlNumbers()
    {
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("setBackupHour"),
            {{QStringLiteral("hour"), 8.0}, {QStringLiteral("expectedHour"), -1.0}}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("setBackupHour"),
            {{QStringLiteral("hour"), 24.0}, {QStringLiteral("expectedHour"), -1}}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("setBackupHour"),
            {{QStringLiteral("hour"), QStringLiteral("8")}, {QStringLiteral("expectedHour"), -1}}));
    }

    void backupIdsAreOpaqueAndBounded()
    {
        const QString id = BackupOperationPolicy::backupIdForFilename(QStringLiteral("shots_backup_20261022.zip"));
        QVERIFY(BackupOperationPolicy::validBackupId(id));
        QCOMPARE(id, BackupOperationPolicy::backupIdForFilename(QStringLiteral("shots_backup_20261022.zip")));
        QVERIFY(BackupOperationPolicy::safeBackupFilename(QStringLiteral("shots_backup_20261022.db")));
        QVERIFY(!BackupOperationPolicy::safeBackupFilename(QStringLiteral("shots_backup_20260231.zip")));
        QVERIFY(!BackupOperationPolicy::safeBackupFilename(QStringLiteral("../shots_backup_20261022.zip")));
        QVERIFY(!BackupOperationPolicy::safeBackupFilename(QStringLiteral("/Users/test/shots_backup_20261022.zip")));
        QVERIFY(!BackupOperationPolicy::validBackupId(QString(64, QLatin1Char('G'))));
        QVERIFY(!BackupOperationPolicy::validBackupId(QStringLiteral("shots_backup_20261022.zip")));
    }

    void asynchronousCompletionPreservesRequestIdentity_data()
    {
        QTest::addColumn<bool>("restore");
        QTest::addColumn<bool>("success");
        QTest::newRow("backup-created") << false << true;
        QTest::newRow("backup-failed") << false << false;
        QTest::newRow("restore-completed") << true << true;
        QTest::newRow("restore-failed") << true << false;
    }

    void asynchronousCompletionPreservesRequestIdentity()
    {
        QFETCH(bool, restore);
        QFETCH(bool, success);
        NativeBackupServiceSource source(nullptr, nullptr);
        const QString id = QStringLiteral("backup-request");
        const QString operation = restore ? QStringLiteral("restoreBackup")
                                          : QStringLiteral("createBackup");
        QCOMPARE(source.m_requests.begin(id), ServiceRequestIds::Start::Accepted);
        source.m_activeId = id;
        source.m_activeOperation = operation;
        QSignalSpy completed(&source, &BackupStateSource::operationCompleted);

        if (restore && success)
            source.managerRestoreCompleted(QStringLiteral("shots_backup_20261022.zip"));
        else if (restore)
            source.managerRestoreFailed(QStringLiteral("Restore failed"));
        else if (success)
            source.managerBackupCreated(QStringLiteral("shots_backup_20261022.zip"));
        else
            source.managerBackupFailed(QStringLiteral("Backup failed"));

        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), id);
        QCOMPARE(completed.first().at(1).toString(), operation);
        QCOMPARE(completed.first().at(2).toBool(), success);
        QCOMPARE(source.snapshot().value(QStringLiteral("lastResult")).toMap()
                     .value(QStringLiteral("operation")).toString(), operation);
        QVERIFY(source.m_activeId.isEmpty());
        QVERIFY(source.m_activeOperation.isEmpty());
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(0));
    }
};

QTEST_GUILESS_MAIN(tst_BackupOperationPolicy)
#include "tst_backupoperationpolicy.moc"
