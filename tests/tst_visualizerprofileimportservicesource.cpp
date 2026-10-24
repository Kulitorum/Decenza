#include "remote/visualizerprofileimportservicesource.h"
#include "network/visualizerimporter.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QtTest>

class tst_visualizerprofileimportservicesource final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QTest::failOnWarning(); }

    void batchPerShotSignalsDoNotCompleteBatchRequest()
    {
        QNetworkAccessManager network;
        VisualizerImporter importer(&network, nullptr, nullptr);
        NativeVisualizerProfileImportServiceSource source(&importer);
        QSignalSpy completed(&source,
                             &VisualizerProfileImportStateSimpleSource::operationCompleted);

        QVERIFY(source.m_requests.begin(QStringLiteral("batch-1"))
                == ServiceRequestIds::Start::Accepted);
        source.m_activeImportId = QStringLiteral("batch-1");
        source.m_activeImportOperation = QStringLiteral("importSelectedShots");

        // The canonical batch importer reports a terminal batchImportComplete;
        // any single-item import signal must not consume its correlated request.
        emit importer.importSuccess(QStringLiteral("one profile"));
        emit importer.importFailed(QStringLiteral("one item failed"));
        QCOMPARE(completed.size(), 0);
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(1));

        emit importer.batchImportComplete(1, 0, 1);
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> result = completed.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("batch-1"));
        QCOMPARE(result.at(1).toString(), QStringLiteral("importSelectedShots"));
        QCOMPARE(result.at(2).toBool(), false);
        QCOMPARE(result.at(3).toMap().value(QStringLiteral("imported")).toInt(), 1);
        QCOMPARE(result.at(3).toMap().value(QStringLiteral("failed")).toInt(), 1);
        QCOMPARE(result.at(4).toString(), QStringLiteral("someProfilesFailed"));
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(0));
        QVERIFY(source.m_activeImportId.isEmpty());
    }

    void canonicalBatchImporterUsesBatchTerminalSignal()
    {
        QNetworkAccessManager network;
        VisualizerImporter importer(&network, nullptr, nullptr);
        QSignalSpy singles(&importer, &VisualizerImporter::importSuccess);
        QSignalSpy batches(&importer, &VisualizerImporter::batchImportComplete);

        // Empty input is the canonical importer's immediate terminal case and
        // exercises its actual signal contract without issuing any network call.
        importer.importSelectedShots({}, false);
        QCOMPARE(singles.size(), 0);
        QCOMPARE(batches.size(), 1);
        QCOMPARE(batches.at(0).at(0).toInt(), 0);
        QCOMPARE(batches.at(0).at(1).toInt(), 0);
        QCOMPARE(batches.at(0).at(2).toInt(), 0);
    }

    void unavailableOperationCompletesOnlyAfterRequestIsTracked()
    {
        NativeVisualizerProfileImportServiceSource source(nullptr);
        QSignalSpy completed(&source,
                             &VisualizerProfileImportStateSimpleSource::operationCompleted);
        source.execute(QStringLiteral("read-1"), QStringLiteral("fetchSharedShots"), {});
        QTRY_COMPARE(completed.size(), 1);
        const QList<QVariant> result = completed.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("read-1"));
        QCOMPARE(result.at(1).toString(), QStringLiteral("fetchSharedShots"));
        QCOMPARE(result.at(2).toBool(), false);
        QCOMPARE(result.at(4).toString(), QStringLiteral("unavailable"));
    }

    void invalidDuplicateResolutionPreservesOriginalRequest()
    {
        QNetworkAccessManager network;
        VisualizerImporter importer(&network, nullptr, nullptr);
        NativeVisualizerProfileImportServiceSource source(&importer);
        QSignalSpy rejected(&source,
                            &VisualizerProfileImportStateSimpleSource::requestRejected);
        QSignalSpy completed(&source,
                             &VisualizerProfileImportStateSimpleSource::operationCompleted);
        QVERIFY(source.m_requests.begin(QStringLiteral("import-1"))
                == ServiceRequestIds::Start::Accepted);
        source.m_activeImportId = QStringLiteral("import-1");
        source.m_activeImportOperation = QStringLiteral("importFromShareCode");
        source.m_duplicateRequestId = QStringLiteral("import-1");

        source.execute(QStringLiteral("import-1"), QStringLiteral("saveWithNewName"),
                       {{QStringLiteral("name"), QString(121, QLatin1Char('x'))}});

        QCOMPARE(rejected.size(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), QStringLiteral("import-1"));
        QCOMPARE(rejected.at(0).at(1).toString(), QStringLiteral("saveWithNewName"));
        QCOMPARE(rejected.at(0).at(2).toString(), QStringLiteral("invalidName"));
        QCOMPARE(completed.size(), 0);
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(1));
        QCOMPARE(source.m_duplicateRequestId, QStringLiteral("import-1"));
    }
};

QTEST_GUILESS_MAIN(tst_visualizerprofileimportservicesource)
#include "tst_visualizerprofileimportservicesource.moc"
