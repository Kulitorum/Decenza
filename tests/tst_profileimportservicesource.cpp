#include "remote/profileimportservicesource.h"
#include "profile/profileimporter.h"
#include "remote/importservicesource.h"
#include "remote/filebundlepolicy.h"
#include "history/shotimporter.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QQmlEngine>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QtTest>

#include "remote/profileimportservice_qml.h"
#include "rep_profileimportservice_source.h"

class TestProfileImportStateSource final : public ProfileImportStateSimpleSource
{
    Q_OBJECT
public:
    using ProfileImportStateSimpleSource::setReady;
    int calls = 0;
    void refresh() override {}
    void execute(QString requestId, QString operation, QVariantMap) override
    {
        ++calls;
        emit operationCompleted(requestId, operation, true, {}, {});
    }
};

class tst_profileimportservicesource final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QTest::failOnWarning(); }

    void refreshDoesNotStartAnUnrequestedScan()
    {
        ProfileImporter importer(nullptr, nullptr);
        NativeProfileImportServiceSource source(&importer, nullptr);
        QSignalSpy scanSpy(&importer, &ProfileImporter::scanComplete);
        source.refresh();
        QCOMPARE(scanSpy.size(), 0);
        QVERIFY(!importer.isScanning());
        QVERIFY(!source.ready());
    }

    void facadeReportsNotReadyUntilHostSnapshotIsReady()
    {
        TestProfileImportStateSource source;
        QRemoteObjectHost host;
        QRemoteObjectNode node;
        QQmlEngine engine;
        QVERIFY(host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0"))));
        QVERIFY(host.enableRemoting<ProfileImportStateSourceAPI>(&source));
        QVERIFY(node.connectToNode(host.hostUrl()));
        ProfileImportService::setPublishedNode(&node);
        auto* service = ProfileImportServiceForeign::create(&engine, &engine);
        QTRY_VERIFY(!service->ready());
        QVERIFY(service->execute(QStringLiteral("scanProfiles")).isEmpty());
        QCOMPARE(source.calls, 0);
        source.setReady(true);
        QTRY_VERIFY(service->ready());
        const QString id = service->execute(QStringLiteral("scanProfiles"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(source.calls, 1);
        ProfileImportService::setPublishedNode(nullptr);
    }

    void cancelDuplicateCompletesOriginalRequestAndClearsOwner()
    {
        ProfileImporter importer(nullptr, nullptr);
        NativeImportServiceSource transfer(nullptr, nullptr, nullptr);
        NativeProfileImportServiceSource source(&importer, &transfer);
        QSignalSpy completed(&source, &ProfileImportStateSimpleSource::operationCompleted);

        QVERIFY(source.m_requests.begin(QStringLiteral("request-1"))
                == ServiceRequestIds::Start::Accepted);
        source.m_activeId = QStringLiteral("request-1");
        source.m_activeOperation = QStringLiteral("importProfile");
        source.m_duplicateId = QStringLiteral("request-1");
        source.execute(QStringLiteral("request-1"), QStringLiteral("cancelImport"), {});

        QCOMPARE(completed.size(), 1);
        const QList<QVariant> values = completed.takeFirst();
        QCOMPARE(values.at(0).toString(), QStringLiteral("request-1"));
        QCOMPARE(values.at(1).toString(), QStringLiteral("importProfile"));
        QCOMPARE(values.at(2).toBool(), false);
        QCOMPARE(values.at(4).toString(), QStringLiteral("cancelled"));
        QVERIFY(source.m_activeId.isEmpty());
        QVERIFY(source.m_activeOperation.isEmpty());
        QVERIFY(source.m_duplicateId.isEmpty());
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(0));
    }

    void batchCompletionCannotBeClaimedBySingleItemSignal()
    {
        ProfileImporter importer(nullptr, nullptr);
        NativeImportServiceSource transfer(nullptr, nullptr, nullptr);
        NativeProfileImportServiceSource source(&importer, &transfer);
        QSignalSpy completed(&source, &ProfileImportStateSimpleSource::operationCompleted);

        QVERIFY(source.m_requests.begin(QStringLiteral("batch-1"))
                == ServiceRequestIds::Start::Accepted);
        source.m_activeId = QStringLiteral("batch-1");
        source.m_activeOperation = QStringLiteral("importAllNew");

        emit importer.importSuccess(QStringLiteral("unrelated single import"));
        QCOMPARE(completed.size(), 0);
        QCOMPARE(source.m_activeId, QStringLiteral("batch-1"));

        emit importer.batchImportComplete(2, 1, 0);
        QCOMPARE(completed.size(), 1);
        const QList<QVariant> values = completed.takeFirst();
        QCOMPARE(values.at(0).toString(), QStringLiteral("batch-1"));
        QCOMPARE(values.at(1).toString(), QStringLiteral("importAllNew"));
        QCOMPARE(values.at(2).toBool(), true);
        QCOMPARE(values.at(3).toMap().value(QStringLiteral("imported")).toInt(), 2);
        QVERIFY(source.m_activeId.isEmpty());
    }

    void invalidDuplicateResolutionKeepsDraftRequestPending()
    {
        ProfileImporter importer(nullptr, nullptr);
        NativeImportServiceSource transfer(nullptr, nullptr, nullptr);
        NativeProfileImportServiceSource source(&importer, &transfer);
        QSignalSpy rejected(&source, &ProfileImportStateSimpleSource::requestRejected);
        QSignalSpy completed(&source, &ProfileImportStateSimpleSource::operationCompleted);

        QVERIFY(source.m_requests.begin(QStringLiteral("duplicate-1"))
                == ServiceRequestIds::Start::Accepted);
        source.m_activeId = QStringLiteral("duplicate-1");
        source.m_activeOperation = QStringLiteral("importProfile");
        source.m_duplicateId = QStringLiteral("duplicate-1");
        source.execute(QStringLiteral("duplicate-1"), QStringLiteral("saveWithNewName"),
                       {{QStringLiteral("name"), QString(121, QLatin1Char('x'))}});

        QCOMPARE(rejected.size(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), QStringLiteral("duplicate-1"));
        QCOMPARE(rejected.at(0).at(1).toString(), QStringLiteral("saveWithNewName"));
        QCOMPARE(rejected.at(0).at(2).toString(), QStringLiteral("invalidName"));
        QCOMPARE(completed.size(), 0);
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(1));
        QCOMPARE(source.m_duplicateId, QStringLiteral("duplicate-1"));
    }

    void languageTransferRejectsMultipleWrongTypeAndOversizedFiles()
    {
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource transfer(&shots, nullptr);
        // Exercise byte transfer independently of DE1-app directory discovery.
        transfer.m_profiles = &profiles;
        QSignalSpy completed(&transfer, &ImportStateSimpleSource::operationCompleted);
        const QVariantMap json{{QStringLiteral("name"), QStringLiteral("translations.json")},
                               {QStringLiteral("size"), 2}};
        QVariantMap large = json;
        large.insert(QStringLiteral("size"), FileBundlePolicy::MaximumLanguageJsonBytes + 1);
        QVariantMap wrongType = json;
        wrongType.insert(QStringLiteral("name"), QStringLiteral("translations.zip"));
        QVariantMap wrongSize = json;
        wrongSize.insert(QStringLiteral("size"), QStringLiteral("2"));
        const QList<QVariantList> invalidSelections{{json, json}, {large}, {wrongType}, {wrongSize}};
        for (qsizetype i = 0; i < invalidSelections.size(); ++i) {
            transfer.execute(QStringLiteral("language-invalid-%1").arg(i), QStringLiteral("beginTransfer"),
                {{QStringLiteral("kind"), QStringLiteral("languageJson")},
                 {QStringLiteral("files"), invalidSelections.at(i)}});
            QCOMPARE(completed.size(), i + 1);
            QVERIFY(!completed.last().at(2).toBool());
        }
    }

    void languageTransferMustBeCompleteBeforeItsSingleUseHostHandoff()
    {
        ShotImporter shots(nullptr);
        ProfileImporter profiles(nullptr, nullptr);
        NativeImportServiceSource transfer(&shots, nullptr);
        transfer.m_profiles = &profiles;
        QSignalSpy completed(&transfer, &ImportStateSimpleSource::operationCompleted);
        transfer.execute(QStringLiteral("language-begin"), QStringLiteral("beginTransfer"),
            {{QStringLiteral("kind"), QStringLiteral("languageJson")},
             {QStringLiteral("files"), QVariantList{QVariantMap{
                {QStringLiteral("name"), QStringLiteral("translations.json")},
                {QStringLiteral("size"), 2}}}}});
        QCOMPARE(completed.size(), 1);
        QVERIFY(completed.last().at(2).toBool());
        const QString id = completed.last().at(3).toMap().value(QStringLiteral("transferId")).toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(!transfer.takeCompletedTransfer(id, QStringLiteral("languageJson")));
        transfer.execute(QStringLiteral("language-incomplete"), QStringLiteral("commitTransfer"),
                         {{QStringLiteral("transferId"), id}});
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("incompleteTransfer"));
        transfer.execute(QStringLiteral("language-append"), QStringLiteral("appendTransferChunk"),
            {{QStringLiteral("transferId"), id}, {QStringLiteral("fileIndex"), 0},
             {QStringLiteral("offset"), 0}, {QStringLiteral("data"), QByteArrayLiteral("{}")}});
        QVERIFY(completed.last().at(2).toBool());
        transfer.execute(QStringLiteral("language-commit"), QStringLiteral("commitTransfer"),
                         {{QStringLiteral("transferId"), id}});
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(!transfer.takeCompletedTransfer(id, QStringLiteral("profileFiles")));
        auto directory = transfer.takeCompletedTransfer(id, QStringLiteral("languageJson"));
        QVERIFY(directory && directory->isValid());
        const QStringList names = QDir(directory->path()).entryList(QDir::Files);
        QCOMPARE(names.size(), 1);
        QFile file(directory->filePath(names.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("{}"));
        QVERIFY(!transfer.takeCompletedTransfer(id, QStringLiteral("languageJson")));
    }
};

QTEST_GUILESS_MAIN(tst_profileimportservicesource)
#include "tst_profileimportservicesource.moc"
