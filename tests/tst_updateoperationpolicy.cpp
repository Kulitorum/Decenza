#include "remote/updateoperationpolicy.h"

#include <QtTest>
#include <QTemporaryDir>

class tst_UpdateOperationPolicy final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void onlyKnownHostOperationsAreAccepted()
    {
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("checkAll")));
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("startFirmware")));
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("downloadAndInstall")));
        QVERIFY(UpdateOperationPolicy::supported(QStringLiteral("installLocalApk")));
        QVERIFY(!UpdateOperationPolicy::supported(QStringLiteral("fetchUrl")));
        QVERIFY(!UpdateOperationPolicy::supported(QStringLiteral("writeFile")));
        QVERIFY(!UpdateOperationPolicy::supported(QStringLiteral("")));
    }

    void localApkAcceptsOnlyCompletedTransferIdentity()
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(UpdateOperationPolicy::validArguments(QStringLiteral("installLocalApk"),
            {{QStringLiteral("transferId"), id}}));
        for (const QVariant& invalid : {QVariant(42), QVariant(QStringLiteral("/tmp/upload.apk")),
                                       QVariant(QString(36, QLatin1Char('-'))), QVariant(QString())})
            QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("installLocalApk"),
                {{QStringLiteral("transferId"), invalid}}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("installLocalApk"),
            {{QStringLiteral("transferId"), id}, {QStringLiteral("path"), QStringLiteral("/tmp/upload.apk")}}));
    }

    void localApkValidatesActualTransferredFile_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("apk") << QStringLiteral("0_upload.apk") << QByteArrayLiteral("PK\x03\x04payload") << true;
        QTest::newRow("uppercase") << QStringLiteral("0_upload.APK") << QByteArrayLiteral("PK\x03\x04payload") << true;
        QTest::newRow("empty") << QStringLiteral("0_upload.apk") << QByteArray() << false;
        QTest::newRow("fake") << QStringLiteral("0_upload.apk") << QByteArrayLiteral("not an APK") << false;
        QTest::newRow("wrong-extension") << QStringLiteral("0_upload.zip") << QByteArrayLiteral("PK\x03\x04payload") << false;
    }

    void localApkValidatesActualTransferredFile()
    {
        QFETCH(QString, name);
        QFETCH(QByteArray, bytes);
        QFETCH(bool, accepted);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        QString reason;
        const QString result = UpdateOperationPolicy::validatedLocalApkPath(directory.path(), &reason);
        QCOMPARE(!result.isEmpty(), accepted);
        QCOMPARE(reason.isEmpty(), accepted);
        if (accepted) QCOMPARE(result, file.fileName());
    }

    void localApkRejectsSizeEscapeAndAmbiguousSelection()
    {
        QTemporaryDir directory;
        QTemporaryDir outside;
        QVERIFY(directory.isValid() && outside.isValid());
        QString reason;
        QVERIFY(UpdateOperationPolicy::validatedLocalApkPath(directory.path(), &reason).isEmpty());
        QFile file(directory.filePath(QStringLiteral("0_upload.apk")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("PK\x03\x04")), qint64(4));
        QVERIFY(file.resize(FileBundlePolicy::MaximumApkBytes + 1));
        file.close();
        QVERIFY(UpdateOperationPolicy::validatedLocalApkPath(directory.path(), &reason).isEmpty());
        QCOMPARE(reason, QStringLiteral("invalidApk"));
        QVERIFY(file.remove());
        QFile external(outside.filePath(QStringLiteral("external.apk")));
        QVERIFY(external.open(QIODevice::WriteOnly));
        QCOMPARE(external.write(QByteArrayLiteral("PK\x03\x04")), qint64(4));
        external.close();
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(external.fileName(), file.fileName()));
        QVERIFY(UpdateOperationPolicy::validatedLocalApkPath(directory.path(), &reason).isEmpty());
        QVERIFY(file.remove());
#endif
        QVERIFY(QFile::copy(external.fileName(), file.fileName()));
        QVERIFY(QFile::copy(external.fileName(), directory.filePath(QStringLiteral("1_second.apk"))));
        QVERIFY(UpdateOperationPolicy::validatedLocalApkPath(directory.path(), &reason).isEmpty());
        QCOMPARE(reason, QStringLiteral("invalidTransfer"));
    }

    void operationArgumentsAreClosedAndTyped()
    {
        QVERIFY(UpdateOperationPolicy::validArguments(QStringLiteral("checkAll"), {}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("checkAll"),
                                                       {{QStringLiteral("url"), QStringLiteral("https://example.invalid")}}));
        QVERIFY(UpdateOperationPolicy::validArguments(QStringLiteral("setTranslationUploadEnabled"),
                                                      {{QStringLiteral("enabled"), true}}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("setTranslationUploadEnabled"),
                                                       {{QStringLiteral("enabled"), QStringLiteral("true")}}));
        QVERIFY(!UpdateOperationPolicy::validArguments(QStringLiteral("setTranslationUploadEnabled"),
                                                       {{QStringLiteral("enabled"), true},
                                                        {QStringLiteral("extra"), 1}}));
    }
};

QTEST_GUILESS_MAIN(tst_UpdateOperationPolicy)
#include "tst_updateoperationpolicy.moc"
