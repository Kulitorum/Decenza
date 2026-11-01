#include "remote/backupoperationpolicy.h"
#include "remote/backupservicesource.h"
#include "core/databasebackupmanager.h"
#include "core/fullbackuprestore.h"
#include "core/profilestorage.h"
#include "core/settings.h"
#include "core/appsettings.h"
#include "core/translationmanager.h"
#include "ai/aimanager.h"
#include "ai/conversationkey.h"
#include "history/shothistorystorage.h"
#include "screensaver/screensavervideomanager.h"

#include <QtTest>
#include <QNetworkAccessManager>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QUuid>
#include <QFile>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <cstring>
#include <QtEndian>
#include "core/ziparchive.h"

class tst_BackupOperationPolicy final : public QObject
{
    Q_OBJECT
    using ArchiveEntries = QList<QPair<QByteArray, QByteArray>>;

    static QString writeArchive(const QString& filename,
                                const QList<QPair<QByteArray, QByteArray>>& entries,
                                const QByteArray& trailer = {})
    {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) return QString();
        file.write("DCBK", 4);
        uchar number[4];
        qToLittleEndian<quint32>(1, number);
        file.write(reinterpret_cast<const char*>(number), 4);
        qToLittleEndian<quint32>(static_cast<quint32>(entries.size()), number);
        file.write(reinterpret_cast<const char*>(number), 4);
        for (const auto& entry : entries) {
            qToLittleEndian<quint32>(static_cast<quint32>(entry.first.size()), number);
            file.write(reinterpret_cast<const char*>(number), 4);
            file.write(entry.first);
            uchar length[8];
            qToLittleEndian<quint64>(static_cast<quint64>(entry.second.size()), length);
            file.write(reinterpret_cast<const char*>(length), 8);
            file.write(entry.second);
        }
        file.write(trailer);
        file.close();
        return file.fileName();
    }

private slots:
    void fullRestoreTransfersLanguage_data()
    {
        QTest::addColumn<bool>("merge");
        QTest::newRow("merge") << true;
        QTest::newRow("replace") << false;
    }

    void fullRestoreTransfersLanguage()
    {
        QFETCH(bool, merge);
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             + QStringLiteral("/translations")).removeRecursively();
        QTemporaryDir directory;
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));
        QNetworkAccessManager network;
        TranslationManager translations(&network, &settings);
        translations.registerString(QStringLiteral("fixture.source"), QStringLiteral("Pitcher"));
        translations.registerString(QStringLiteral("fixture.local"), QStringLiteral("Local"));
        translations.setTranslation(QStringLiteral("fixture.local"), QStringLiteral("Local edit"));
        const QJsonObject extra{
            {QStringLiteral("language"), QStringLiteral("en")},
            {QStringLiteral("languageTranslation"), QJsonObject{
                {QStringLiteral("language"), QStringLiteral("en")},
                {QStringLiteral("translations"), QJsonObject{
                    {QStringLiteral("fixture.source"), QStringLiteral("Jug")}}}}},
            {QStringLiteral("languageOverrides"), QJsonArray{}}};
        const QString path = writeArchive(directory.filePath(QStringLiteral("language.dcbackup")),
            {{QByteArrayLiteral("extra_settings.json"), QJsonDocument(extra).toJson()}});
        QVERIFY(!path.isEmpty());
        FullBackupRestore restore(&settings, nullptr, nullptr, nullptr, nullptr, &translations);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = merge;
        options.shots = options.profiles = options.media = false;
        QVERIFY(restore.restoreFile(QStringLiteral("language"), path, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(3).toString()));
        QCOMPARE(translations.translateString(QStringLiteral("fixture.source"), QStringLiteral("Pitcher")), QStringLiteral("Jug"));
        QCOMPARE(translations.translateString(QStringLiteral("fixture.local"), QStringLiteral("Local")),
                 merge ? QStringLiteral("Local edit") : QStringLiteral("Local"));
    }

    void zipRestoreTransfersLanguage()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             + QStringLiteral("/translations")).removeRecursively();
        QTemporaryDir directory;
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));
        QNetworkAccessManager network;
        TranslationManager translations(&network, &settings);
        translations.registerString(QStringLiteral("fixture.source"), QStringLiteral("Pitcher"));
        translations.registerString(QStringLiteral("fixture.local"), QStringLiteral("Local"));
        translations.setTranslation(QStringLiteral("fixture.local"), QStringLiteral("Local edit"));
        const QJsonObject backup{
            {QStringLiteral("languageTranslation"), QJsonObject{
                {QStringLiteral("language"), QStringLiteral("en")},
                {QStringLiteral("translations"), QJsonObject{
                    {QStringLiteral("fixture.source"), QStringLiteral("Jug")}}}}},
            {QStringLiteral("languageOverrides"), QJsonArray{}}};
        const QString zipPath = directory.filePath(QStringLiteral("language.zip"));
        QuaZip writer(zipPath);
        QVERIFY(writer.open(QuaZip::mdCreate));
        QVERIFY(ZipArchive::addFile(writer, QStringLiteral("settings.json"), QJsonDocument(backup).toJson()));
        writer.close();
        QCOMPARE(writer.getZipError(), UNZ_OK);
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(directory.filePath(QStringLiteral("shots.db"))));
        DatabaseBackupManager manager(&settings, &storage);
        manager.setTranslationManager(&translations);
        QSignalSpy finished(&manager, &DatabaseBackupManager::restoreCompleted);
        QSignalSpy failed(&manager, &DatabaseBackupManager::restoreFailed);
        QVERIFY(manager.restoreBackupFile(zipPath, false, false, true, false, false));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() == 1, 10000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QCOMPARE(translations.translateString(QStringLiteral("fixture.source"), QStringLiteral("Pitcher")), QStringLiteral("Jug"));
        QCOMPARE(translations.translateString(QStringLiteral("fixture.local"), QStringLiteral("Local")), QStringLiteral("Local"));
        manager.m_backupDirectoryForTests = directory.filePath(QStringLiteral("backups"));
        QVERIFY(QDir().mkpath(manager.m_backupDirectoryForTests));
        QSignalSpy created(&manager, &DatabaseBackupManager::backupCreated);
        QVERIFY(manager.createBackup(true));
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 10000);
        const QString extracted = directory.filePath(QStringLiteral("inspect"));
        QVERIFY(ZipArchive::extract(created.first().first().toString(), extracted));
        QFile savedSettings(QDir(extracted).filePath(QStringLiteral("settings.json")));
        QVERIFY(savedSettings.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(savedSettings.readAll()).object();
        QCOMPARE(saved.value(QStringLiteral("languageTranslation")).toObject()
                     .value(QStringLiteral("translations")).toObject()
                     .value(QStringLiteral("fixture.source")).toString(), QStringLiteral("Jug"));
        storage.close();
        QTRY_VERIFY(storage.isDbWorkIdle());
    }

    void olderBackupDoesNotEraseLanguageCustomizations()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
             + QStringLiteral("/translations")).removeRecursively();
        QTemporaryDir directory;
        Settings settings;
        settings.setValue(QStringLiteral("localization/language"), QStringLiteral("en"));
        QNetworkAccessManager network;
        TranslationManager translations(&network, &settings);
        translations.registerString(QStringLiteral("fixture.local"), QStringLiteral("Local"));
        translations.setTranslation(QStringLiteral("fixture.local"), QStringLiteral("Keep me"));
        const QByteArray extra = QJsonDocument(QJsonObject{
            {QStringLiteral("language"), QStringLiteral("en")}}).toJson();
        const QString path = writeArchive(directory.filePath(QStringLiteral("old.dcbackup")),
            {{QByteArrayLiteral("extra_settings.json"), extra}});
        FullBackupRestore restore(&settings, nullptr, nullptr, nullptr, nullptr, &translations);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = false;
        options.shots = options.profiles = options.media = false;
        QVERIFY(restore.restoreFile(QStringLiteral("old"), path, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(translations.translateString(QStringLiteral("fixture.local"), QStringLiteral("Local")), QStringLiteral("Keep me"));
        finished.clear();
        const QString badPath = writeArchive(directory.filePath(QStringLiteral("invalid-language.dcbackup")),
            {{QByteArrayLiteral("extra_settings.json"), QJsonDocument(QJsonObject{
                {QStringLiteral("languageTranslationError"), true}}).toJson()}});
        QVERIFY(restore.restoreFile(QStringLiteral("bad-language"), badPath, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(translations.translateString(QStringLiteral("fixture.local"), QStringLiteral("Local")), QStringLiteral("Keep me"));
    }

    void zipStreamsLargeEntries_data()
    {
        QTest::addColumn<int>("level");
        QTest::newRow("stored") << 0;
        QTest::newRow("deflate-6") << 6;
        QTest::newRow("deflate-9") << 9;
    }

    void zipStreamsLargeEntries()
    {
        QFETCH(int, level);
        class RepeatedInput final : public QIODevice {
        public:
            RepeatedInput() { open(QIODevice::ReadOnly); }
            qint64 size() const override { return 160LL * 1024 * 1024; }
            qint64 largestRead = 0;
        protected:
            qint64 readData(char* bytes, qint64 maximum) override {
                largestRead = qMax(largestRead, maximum);
                const qint64 count = qMin(maximum, size() - pos());
                std::memset(bytes, 'a', size_t(count));
                return count;
            }
            qint64 writeData(const char*, qint64) override { return -1; }
        } input;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("large.zip"));
        QuaZip archive(path);
        QVERIFY(archive.open(QuaZip::mdCreate));
        QElapsedTimer timer;
        timer.start();
        QVERIFY(ZipArchive::addFile(archive, QStringLiteral("nested/café.bin"), input,
                                    level, level == 0 ? 0 : Z_DEFLATED));
        archive.close();
        QCOMPARE(archive.getZipError(), UNZ_OK);
        QVERIFY(input.largestRead <= ZipArchive::ChunkBytes);
        qInfo() << "ZIP level" << level << "bytes" << QFileInfo(path).size()
                << "write ms" << timer.elapsed();
        const QString outputDir = directory.filePath(QStringLiteral("extracted"));
        QVERIFY(ZipArchive::extract(path, outputDir));
        QFile output(QDir(outputDir).filePath(QStringLiteral("nested/café.bin")));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.size(), input.size());
        const QByteArray expected(ZipArchive::ChunkBytes, 'a');
        while (!output.atEnd()) QCOMPARE(output.read(expected.size()), expected);
    }

    void zipReadsIndependentArchiveAndRejectsCorruption_data()
    {
        QTest::addColumn<QByteArray>("original");
        QTest::newRow("standard") << QByteArray::fromBase64(
            "UEsDBBQAAAAAAAAAWF2lrJ1GDgAAAA4AAAAKAAAAbGVnYWN5LnR4dGxlZ2FjeSBiYWNrdXAK"
            "UEsBAhQDFAAAAAAAAABYXaWsnUYOAAAADgAAAAoAAAAAAAAAAAAAAIABAAAAAGxlZ2FjeS50eHRQ"
            "SwUGAAAAAAEAAQA4AAAANgAAAAAA");
        QTest::newRow("zip64") << QByteArray::fromBase64("UEsDBC0AAAAAAAAAWF2lrJ1G//////////8KABQAbGVnYWN5LnR4dAEAEAAOAAAAAAAAAA4AAAAAAAAAbGVnYWN5IGJhY2t1cApQSwECLQMtAAAAAAAAAFhdpaydRg4AAAAOAAAACgAAAAAAAAAAAAAAgAEAAAAAbGVnYWN5LnR4dFBLBQYAAAAAAQABADgAAABKAAAAAAA=");
    }

    void zipReadsIndependentArchiveAndRejectsCorruption()
    {
        QFETCH(QByteArray, original);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("legacy.zip"));
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        QCOMPARE(archive.write(original), original.size());
        archive.close();
        const QString destination = directory.filePath(QStringLiteral("out"));
        QVERIFY(ZipArchive::extract(path, destination));
        QFile restored(QDir(destination).filePath(QStringLiteral("legacy.txt")));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArrayLiteral("legacy backup\n"));
        QByteArray corrupt = original;
        const qsizetype payload = corrupt.indexOf("legacy backup\n");
        QVERIFY(payload >= 0);
        corrupt[payload] = 'x';
        QVERIFY(archive.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(archive.write(corrupt), corrupt.size());
        archive.close();
        QVERIFY(!ZipArchive::extract(path, directory.filePath(QStringLiteral("corrupt"))));
        QuaZip empty(directory.filePath(QStringLiteral("empty.zip")));
        QVERIFY(empty.open(QuaZip::mdCreate));
        empty.close();
        QVERIFY(ZipArchive::extract(empty.getZipName(), directory.filePath(QStringLiteral("empty"))));
    }

    void init() { QTest::failOnWarning(); }

    void fullBackupArchiveIsValidatedBeforeCompletion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        FullBackupRestore restore(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        QVERIFY(finished.isValid());
        const QString validPath = writeArchive(directory.filePath(QStringLiteral("empty.dcbackup")), {}, {});
        QVERIFY(!validPath.isEmpty());
        QVERIFY(restore.restoreFile(QStringLiteral("valid"), validPath, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 200);

        finished.clear();
        const QString unsafePath = writeArchive(directory.filePath(QStringLiteral("unsafe.dcbackup")),
            {{QByteArrayLiteral("media/../escape"), QByteArrayLiteral("x")}}, {});
        QVERIFY(!unsafePath.isEmpty());
        QVERIFY(restore.restoreFile(QStringLiteral("unsafe"), unsafePath, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 400);
        QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("unsafe entry path")));

        finished.clear();
        const QString trailingPath = writeArchive(directory.filePath(QStringLiteral("trailing.dcbackup")), {}, QByteArrayLiteral("x"));
        QVERIFY(!trailingPath.isEmpty());
        QVERIFY(restore.restoreFile(QStringLiteral("trailing"), trailingPath, {}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 400);
        QVERIFY(finished.first().at(3).toString().contains(QStringLiteral("trailing data")));
    }

    void fullBackupRestoreStagesProfileMediaAndSettingsAfterValidation()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        Settings settings;
        settings.setScaleAddress(QStringLiteral("before-restore"));
        ProfileStorage profiles;
        QNetworkAccessManager network;
        ScreensaverVideoManager media(&network, &settings, &profiles);
        media.m_cacheDir = directory.filePath(QStringLiteral("isolated-media-cache"));
        QVERIFY(QDir().mkpath(media.m_cacheDir));
        media.reloadPersonalMedia();

        FullBackupRestore restore(&settings, nullptr, &profiles, &media, nullptr, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        QVERIFY(finished.isValid());
        QSignalSpy scaleChanged(&settings, &Settings::scaleAddressChanged);
        const QString malformed = writeArchive(directory.filePath(QStringLiteral("settings-trailing.dcbackup")),
            {{QByteArrayLiteral("settings.json"),
              QByteArrayLiteral("{\"scale\":{\"address\":\"must-not-apply\"}}")}},
            QByteArrayLiteral("x"));
        QVERIFY(!malformed.isEmpty());
        FullBackupRestore::Options options;
        options.shots = false;
        QVERIFY(restore.restoreFile(QStringLiteral("malformed"), malformed, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.last().at(1).toBool());
        QCOMPARE(settings.scaleAddress(), QStringLiteral("before-restore"));
        QCOMPARE(scaleChanged.size(), 0);

        finished.clear();
        const QByteArray imageBytes("prepared-png-payload");
        const QString profileName = QStringLiteral("fixture-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString mediaName = QStringLiteral("fixture-%1.png")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString valid = writeArchive(directory.filePath(QStringLiteral("profile-media.dcbackup")), {
            {QByteArrayLiteral("settings.json"),
             QByteArrayLiteral("{\"scale\":{\"address\":\"restored-scale\"}}")},
            {QStringLiteral("profiles/user/%1.json").arg(profileName).toUtf8(),
             QByteArrayLiteral("{\"title\":\"Fixture profile\",\"steps\":[{\"pump\":\"pressure\",\"pressure\":9,\"seconds\":20}]}")},
            {QByteArrayLiteral("media/catalog.json"), QJsonDocument(QJsonArray{QJsonObject{
                {QStringLiteral("path"), QStringLiteral("/old/device/personal/") + mediaName},
                {QStringLiteral("type"), QStringLiteral("image")},
                {QStringLiteral("original_name"), QStringLiteral("Original photo.png")},
                {QStringLiteral("date"), QStringLiteral("2026-10-23T12:00:00Z")},
                {QStringLiteral("duration_s"), 17}}}).toJson()},
            {QStringLiteral("media/%1").arg(mediaName).toUtf8(), imageBytes}});
        QVERIFY(!valid.isEmpty());
        QVERIFY(restore.restoreFile(QStringLiteral("valid-fixture"), valid, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(settings.scaleAddress(), QStringLiteral("restored-scale"));
        QFile profile(QDir(profiles.userProfilesPath()).filePath(profileName + QStringLiteral(".json")));
        QVERIFY(profile.open(QIODevice::ReadOnly));
        QCOMPARE(profile.readAll(), QByteArrayLiteral("{\"title\":\"Fixture profile\",\"steps\":[{\"pump\":\"pressure\",\"pressure\":9,\"seconds\":20}]}"));
        const QVariantList mediaItems = media.getPersonalMediaList();
        QCOMPARE(mediaItems.size(), 1);
        QCOMPARE(mediaItems.first().toMap().value(QStringLiteral("filename")).toString(), QStringLiteral("Original photo.png"));
        QCOMPARE(media.m_personalCatalog.first().durationSeconds, 17);
        QCOMPARE(media.m_personalCatalog.first().sourceUrl, QStringLiteral("2026-10-23T12:00:00Z"));
        finished.clear();
        QVERIFY(restore.restoreFile(QStringLiteral("repeat-merge"), valid, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(media.getPersonalMediaList(), mediaItems);
        QFile restoredMedia(mediaItems.first().toMap().value(QStringLiteral("path")).toString());
        QVERIFY(restoredMedia.open(QIODevice::ReadOnly));
        QCOMPARE(restoredMedia.readAll(), imageBytes);

        finished.clear();
        const QString partialPath = writeArchive(directory.filePath(QStringLiteral("partial-settings.dcbackup")), {
            {QByteArrayLiteral("settings.json"), QByteArrayLiteral("{\"steam\":{\"selectedPitcher\":100000}}")}});
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            QStringLiteral(".*SettingsSerializer.*imported selectedPitcher.*")));
        QVERIFY(restore.restoreFile(QStringLiteral("partial-settings"), partialPath, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 400);
        QCOMPARE(finished.first().at(2).toMap().value(QStringLiteral("settings")).toBool(), false);
        QVERIFY(!finished.first().at(3).toString().isEmpty());

        const QString deletionPath = writeArchive(directory.filePath(QStringLiteral("delete-on-setting-signal.dcbackup")), {
            {QByteArrayLiteral("settings.json"),
             QByteArrayLiteral("{\"scale\":{\"address\":\"delete-coordinator\"}}")}});
        QVERIFY(!deletionPath.isEmpty());
        auto* deletionRestore = new FullBackupRestore(&settings, nullptr, &profiles, &media, nullptr, nullptr);
        QPointer<FullBackupRestore> deletionGuard(deletionRestore);
        connect(&settings, &Settings::scaleAddressChanged, &settings,
                [&deletionRestore] {
                    delete deletionRestore;
                    deletionRestore = nullptr;
                }, Qt::DirectConnection);
        FullBackupRestore::Options settingsOnly;
        settingsOnly.shots = false;
        settingsOnly.profiles = false;
        settingsOnly.media = false;
        QVERIFY(deletionRestore->restoreFile(QStringLiteral("deleted-during-settings"),
                                             deletionPath, settingsOnly));
        QTRY_VERIFY_WITH_TIMEOUT(deletionGuard.isNull(), 5000);
    }

    void shotReplacementRequiresDatabaseBeforeApplyingOtherCategories()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings settings;
        settings.setScaleAddress(QStringLiteral("before-missing-shots"));
        const QString archive = writeArchive(directory.filePath(QStringLiteral("missing-shots.dcbackup")), {
            {QByteArrayLiteral("settings.json"),
             QByteArrayLiteral("{\"scale\":{\"address\":\"must-not-apply\"}}")}});
        QVERIFY(!archive.isEmpty());
        FullBackupRestore restore(&settings, nullptr, nullptr, nullptr, nullptr, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = false;
        QVERIFY(restore.restoreFile(QStringLiteral("missing-shots"), archive, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 400);
        QCOMPARE(settings.scaleAddress(), QStringLiteral("before-missing-shots"));
    }

    void replaceRejectsInvalidPayloadsBeforeMutation_data()
    {
        QTest::addColumn<QList<QPair<QByteArray, QByteArray>>>("entries");
        QTest::newRow("profile-without-frames") << QList<QPair<QByteArray, QByteArray>>{
            {"profiles/user/invalid.json", "{}"}};
        QTest::newRow("profile-case-collision") << QList<QPair<QByteArray, QByteArray>>{
            {"profiles/user/Foo.json", "{}"}, {"profiles/user/foo.json", "{}"}};
        QTest::newRow("conversation-without-key") << QList<QPair<QByteArray, QByteArray>>{
            {"ai_conversations.json", "[{}]"}};
        QTest::newRow("conversation-without-messages") << QList<QPair<QByteArray, QByteArray>>{
            {"ai_conversations.json", "[{\"key\":\"existing\"}]"}};
    }

    void replaceRejectsInvalidPayloadsBeforeMutation()
    {
        QFETCH(ArchiveEntries, entries);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings settings;
        settings.setScaleAddress(QStringLiteral("preserve-before-invalid"));
        entries.prepend({"settings.json", "{\"scale\":{\"address\":\"must-not-apply\"}}"});
        const QString path = writeArchive(directory.filePath(QStringLiteral("invalid.dcbackup")), entries);
        QVERIFY(!path.isEmpty());
        FullBackupRestore restore(&settings, nullptr, nullptr, nullptr, nullptr, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = false;
        options.shots = false;
        QVERIFY(restore.restoreFile(QStringLiteral("invalid-payload"), path, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 400);
        QCOMPARE(settings.scaleAddress(), QStringLiteral("preserve-before-invalid"));
    }

    void replaceRejectsInvalidMediaBeforeClearingExisting_data()
    {
        QTest::addColumn<QStringList>("names");
        QTest::newRow("case-collision") << QStringList{"media/Foo.png", "media/foo.png"};
        QTest::newRow("unsupported-extension") << QStringList{"media/unsupported.txt"};
    }

    void replaceRejectsInvalidMediaBeforeClearingExisting()
    {
        QFETCH(QStringList, names);
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings settings;
        ProfileStorage profiles;
        QNetworkAccessManager network;
        ScreensaverVideoManager media(&network, &settings, &profiles);
        media.m_cacheDir = directory.filePath(QStringLiteral("media-cache"));
        QVERIFY(QDir().mkpath(media.m_cacheDir));
        media.reloadPersonalMedia();
        const QString originalPath = directory.filePath(QStringLiteral("existing.png"));
        const QByteArray originalBytes("existing-personal-image");
        {
            QFile file(originalPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(originalBytes), originalBytes.size());
        }
        QVERIFY(media.addPersonalMedia(originalPath, QStringLiteral("existing.png")));
        const QVariantList originalMedia = media.getPersonalMediaList();
        QCOMPARE(originalMedia.size(), 1);

        QList<QPair<QByteArray, QByteArray>> entries;
        for (const QString& name : names)
            entries.append({name.toUtf8(), QByteArrayLiteral("archive-image")});
        const QString archive = writeArchive(directory.filePath(QStringLiteral("invalid.dcbackup")), entries);
        QVERIFY(!archive.isEmpty());
        FullBackupRestore restore(&settings, nullptr, &profiles, &media, nullptr, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = false;
        options.shots = options.settings = options.profiles = false;
        QVERIFY(restore.restoreFile(QStringLiteral("invalid-media"), archive, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(4).toInt(), 400);
        QCOMPARE(media.getPersonalMediaList(), originalMedia);
        QFile retained(originalMedia.first().toMap().value(QStringLiteral("path")).toString());
        QVERIFY(retained.open(QIODevice::ReadOnly));
        QCOMPARE(retained.readAll(), originalBytes);
    }

    void shotRestoreHonorsConversationReplaceMode_data()
    {
        QTest::addColumn<bool>("merge");
        QTest::addColumn<bool>("emptyConversations");
        QTest::addColumn<bool>("omitConversations");
        QTest::newRow("replace") << false << false << false;
        QTest::newRow("merge") << true << false << false;
        QTest::newRow("replace-empty") << false << true << false;
        QTest::newRow("replace-absent") << false << true << true;
        QTest::newRow("merge-absent") << true << true << true;
    }

    void shotRestoreHonorsConversationReplaceMode()
    {
        QFETCH(bool, merge);
        QFETCH(bool, emptyConversations);
        QFETCH(bool, omitConversations);
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings settings;
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(directory.filePath(QStringLiteral("live.sqlite"))));
        const QString database = directory.filePath(QStringLiteral("shots.db"));
        QVERIFY(!ShotHistoryStorage::createBackupStatic(storage.databasePath(), database).isEmpty());
        QFile databaseFile(database);
        QVERIFY(databaseFile.open(QIODevice::ReadOnly));
        const QByteArray databaseBytes = databaseFile.readAll();
        databaseFile.close();
        QNetworkAccessManager network;
        AIManager ai(&network, &settings);
        AppSettings stored;
        stored.remove(QStringLiteral("ai/conversations"));
        const auto cleanup = qScopeGuard([] {
            AppSettings().remove(QStringLiteral("ai/conversations"));
        });
        const QString existingKey = ConversationKey::derive("Existing", "Bean", "Profile", 0);
        const QString incomingKey = ConversationKey::derive("Incoming", "Bean", "Profile", 0);
        const QByteArray existingMessages("[{\"role\":\"assistant\",\"content\":\"Keep my advice\"}]");
        stored.setValue(QStringLiteral("ai/conversations/index"), QJsonDocument(QJsonArray{
            QJsonObject{{QStringLiteral("key"), existingKey}}}).toJson(QJsonDocument::Compact));
        const QString existingMessagesPath = QStringLiteral("ai/conversations/") + existingKey
            + QStringLiteral("/messages");
        stored.setValue(existingMessagesPath, existingMessages);
        stored.sync();
        ai.reloadConversations();
        QCOMPARE(ai.conversationIndex().size(), 1);
        QJsonArray incoming{QJsonObject{
            {QStringLiteral("key"), incomingKey},
            {QStringLiteral("beanBrand"), QStringLiteral("Incoming")},
            {QStringLiteral("beanType"), QStringLiteral("Bean")},
            {QStringLiteral("profileName"), QStringLiteral("Profile")},
            {QStringLiteral("messages"), QJsonArray{QJsonObject{
                {QStringLiteral("role"), QStringLiteral("assistant")},
                {QStringLiteral("content"), QStringLiteral("Restored advice")}}}}}};
        if (emptyConversations) incoming = {};
        ArchiveEntries entries{{QByteArrayLiteral("shots.db"), databaseBytes}};
        if (!omitConversations)
            entries.append({QByteArrayLiteral("ai_conversations.json"), QJsonDocument(incoming).toJson(QJsonDocument::Compact)});
        const QString archive = writeArchive(directory.filePath(QStringLiteral("shots-ai.dcbackup")), entries);
        QVERIFY(!archive.isEmpty());
        FullBackupRestore restore(&settings, &storage, nullptr, nullptr, &ai, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = merge;
        options.profiles = options.media = false;
        QVERIFY(restore.restoreFile(QStringLiteral("shots-ai"), archive, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(finished.first().at(2).toMap().value(QStringLiteral("aiConversationsImported")).toInt(),
                 emptyConversations ? 0 : 1);
        QSet<QString> keys;
        for (const auto& entry : ai.conversationIndex()) keys.insert(entry.key);
        QCOMPARE(keys.contains(incomingKey), !emptyConversations);
        QCOMPARE(keys.contains(existingKey), merge);
        QCOMPARE(keys.size(), merge ? (emptyConversations ? 1 : 2) : (emptyConversations ? 0 : 1));
        stored.sync();
        QCOMPARE(stored.contains(existingMessagesPath), merge);
        if (merge) QCOMPARE(stored.value(existingMessagesPath).toByteArray(), existingMessages);
    }

    void replaceRemovesOmittedRootProfilesAndPreservesInternalFiles()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString previousName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("restore-root-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto cleanup = qScopeGuard([previousName, appData] {
            QCoreApplication::setApplicationName(previousName);
            QDir(appData).removeRecursively();
        });
        ProfileStorage profiles;
        const QDir root(profiles.fallbackPath());
        QVERIFY(QDir().mkpath(root.path()));
        const QStringList seeded{"old.json", "_current.json", "_internal.json"};
        for (const QString& name : seeded) {
            QFile file(root.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("{\"title\":\"Existing\"}"), qint64(20));
        }
        const QByteArray restoredBytes("{\"title\":\"Restored root profile\",\"steps\":[{\"pump\":\"pressure\",\"pressure\":9,\"seconds\":20}]}");
        const QString archive = writeArchive(directory.filePath(QStringLiteral("root-profile.dcbackup")), {
            {QByteArrayLiteral("profiles/restored.json"), restoredBytes}});
        QVERIFY(!archive.isEmpty());
        FullBackupRestore restore(nullptr, nullptr, &profiles, nullptr, nullptr, nullptr);
        QSignalSpy finished(&restore, &FullBackupRestore::finished);
        FullBackupRestore::Options options;
        options.merge = false;
        options.shots = options.settings = options.media = false;
        QVERIFY(restore.restoreFile(QStringLiteral("root-profile"), archive, options));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.first().at(1).toBool());
        QVERIFY(!QFile::exists(root.filePath(QStringLiteral("old.json"))));
        for (const QString& name : seeded.mid(1)) {
            QFile file(root.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArrayLiteral("{\"title\":\"Existing\"}"));
        }
        QFile restored(root.filePath(QStringLiteral("restored.json")));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), restoredBytes);
    }

    void uploadedNativeZipRestoresSettingsProfileAndMedia()
    {
        QStandardPaths::setTestModeEnabled(true);
        auto uploaded = std::make_shared<QTemporaryDir>();
        QTemporaryDir databaseDirectory;
        QVERIFY(uploaded->isValid());
        QVERIFY(databaseDirectory.isValid());

        const QString previousName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("restore-zip-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto cleanup = qScopeGuard([previousName, appData] {
            QCoreApplication::setApplicationName(previousName);
            QDir(appData).removeRecursively();
        });

        Settings settings;
        ProfileStorage profiles;
        QNetworkAccessManager network;
        ScreensaverVideoManager media(&network, &settings, &profiles);
        media.m_cacheDir = uploaded->filePath(QStringLiteral("isolated-media-cache"));
        QVERIFY(QDir().mkpath(media.m_cacheDir));
        media.reloadPersonalMedia();

        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(databaseDirectory.filePath(QStringLiteral("live.sqlite"))));
        DatabaseBackupManager manager(&settings, &storage, &profiles, &media);
        connect(&manager, &DatabaseBackupManager::mediaRestored,
                &media, &ScreensaverVideoManager::reloadPersonalMedia);
        QSignalSpy restored(&manager, &DatabaseBackupManager::restoreCompleted);
        QSignalSpy failed(&manager, &DatabaseBackupManager::restoreFailed);
        QVERIFY(restored.isValid());
        QVERIFY(failed.isValid());

        const QString profileName = QStringLiteral("uploaded-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString mediaName = QStringLiteral("uploaded-%1.png")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString existingMediaName = QStringLiteral("existing-%1.png")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QByteArray profileBytes("{\"title\":\"Uploaded native ZIP\",\"steps\":[{\"pump\":\"pressure\",\"pressure\":9,\"seconds\":20}]}");
        const QByteArray mediaBytes("native-zip-media-bytes");
        const QByteArray existingMediaBytes("preexisting-personal-media");
        const QString existingMediaPath = uploaded->filePath(existingMediaName);
        {
            QFile file(existingMediaPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(existingMediaBytes), existingMediaBytes.size());
        }
        QVERIFY(media.addPersonalMedia(existingMediaPath, existingMediaName));
        const QString zipPath = uploaded->filePath(QStringLiteral("uploaded-backup.zip"));
        {
            QuaZip writer(zipPath);
            QVERIFY(writer.open(QuaZip::mdCreate));
            ZipArchive::addFile(writer, QStringLiteral("settings.json"), QJsonDocument(QJsonObject{
                {QStringLiteral("scale"), QJsonObject{
                    {QStringLiteral("address"), QStringLiteral("uploaded-zip-scale")}}}})
                .toJson(QJsonDocument::Compact));
            ZipArchive::addFile(writer, QStringLiteral("profiles/user/%1.JSON").arg(profileName), profileBytes);
            ZipArchive::addFile(writer, QStringLiteral("media/%1").arg(mediaName), mediaBytes);
            const QJsonArray mediaCatalog{
                QJsonObject{
                    {QStringLiteral("id"), 1},
                    {QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("path"), mediaName},
                    {QStringLiteral("original_name"), mediaName},
                    {QStringLiteral("duration_s"), 17},
                    {QStringLiteral("author"), QStringLiteral("Personal")},
                    {QStringLiteral("date"), QStringLiteral("2026-10-23T12:00:00Z")},
                    {QStringLiteral("bytes"), mediaBytes.size()}
                }
            };
            ZipArchive::addFile(writer, QStringLiteral("media/catalog.json"),
                           QJsonDocument(mediaCatalog).toJson(QJsonDocument::Compact));
            writer.close();
            QCOMPARE(writer.getZipError(), UNZ_OK);
        }

        const QString invalidZip = uploaded->filePath(QStringLiteral("invalid-profile.zip"));
        {
            QuaZip writer(invalidZip);
            QVERIFY(writer.open(QuaZip::mdCreate));
            QVERIFY(ZipArchive::addFile(writer, QStringLiteral("profiles/user/broken.json"), QByteArrayLiteral("{}")));
            writer.close();
        }
        const QString existingProfile = QDir(profiles.userProfilesPath()).filePath(profileName + QStringLiteral(".json"));
        {
            QFile file(existingProfile);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(profileBytes), profileBytes.size());
        }
        QVERIFY(manager.restoreBackupFile(invalidZip, false, false, false, true, false, uploaded));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        QFile retainedProfile(existingProfile);
        QVERIFY(retainedProfile.open(QIODevice::ReadOnly));
        QCOMPARE(retainedProfile.readAll(), profileBytes);
        retainedProfile.close();
        QVERIFY(QFile::remove(existingProfile));
        failed.clear();

        const QVariantList beforeMissingDatabase = media.getPersonalMediaList();
        settings.setScaleAddress(QStringLiteral("before-missing-zip-database"));
        QVERIFY(manager.restoreBackupFile(zipPath, false, true, true, true, true, uploaded));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        QCOMPARE(restored.size(), 0);
        QCOMPARE(settings.scaleAddress(), QStringLiteral("before-missing-zip-database"));
        QCOMPARE(media.getPersonalMediaList(), beforeMissingDatabase);
        QVERIFY(!QFile::exists(QDir(profiles.userProfilesPath()).filePath(profileName + QStringLiteral(".json"))));
        failed.clear();

        QVERIFY(manager.restoreBackupFile(zipPath, true, false, true, true, true, uploaded));
        QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 1, 10000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(restored.first().at(0).toString(), QStringLiteral("uploaded-backup.zip"));
        QCOMPARE(settings.scaleAddress(), QStringLiteral("uploaded-zip-scale"));
        QFile profile(QDir(profiles.userProfilesPath()).filePath(profileName + QStringLiteral(".JSON")));
        QVERIFY(profile.open(QIODevice::ReadOnly));
        QCOMPARE(profile.readAll(), profileBytes);
        const QVariantList mediaItems = media.getPersonalMediaList();
        QCOMPARE(mediaItems.size(), 2);
        QSet<QString> restoredNames;
        QMap<QString, QByteArray> restoredContents;
        for (const QVariant& item : mediaItems) {
            const QVariantMap entry = item.toMap();
            const QString name = entry.value(QStringLiteral("filename")).toString();
            restoredNames.insert(name);
            QFile restoredMedia(entry.value(QStringLiteral("path")).toString());
            QVERIFY(restoredMedia.open(QIODevice::ReadOnly));
            restoredContents.insert(name, restoredMedia.readAll());
        }
        QVERIFY(restoredNames.contains(mediaName));
        QVERIFY(restoredNames.contains(existingMediaName));
        QCOMPARE(restoredContents.value(mediaName), mediaBytes);
        QCOMPARE(restoredContents.value(existingMediaName), existingMediaBytes);
        QVERIFY(!manager.isRestoreInProgress());

        const QString rawDbPath = uploaded->filePath(QStringLiteral("shots_backup_20261023.db"));
        QVERIFY(!ShotHistoryStorage::createBackupStatic(storage.databasePath(), rawDbPath).isEmpty());
        QFile siblingSettings(uploaded->filePath(QStringLiteral("settings.json")));
        QVERIFY(siblingSettings.open(QIODevice::WriteOnly));
        siblingSettings.write("{\"scale\":{\"address\":\"unrelated-sibling-settings\"}}");
        siblingSettings.close();
        QVERIFY(manager.restoreBackupFile(rawDbPath, false, true, true, true, true, uploaded));
        QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 2, 10000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(restored.last().at(0).toString(), QStringLiteral("shots_backup_20261023.db"));
        QCOMPARE(settings.scaleAddress(), QStringLiteral("uploaded-zip-scale"));
        QVERIFY(QFile::exists(profile.fileName()));
        QCOMPARE(media.getPersonalMediaList(), mediaItems);
        QVERIFY(!manager.isRestoreInProgress());

        manager.m_backupDirectoryForTests = uploaded->filePath(QStringLiteral("snapshots"));
        QVERIFY(QDir().mkpath(manager.m_backupDirectoryForTests));
        QSignalSpy created(&manager, &DatabaseBackupManager::backupCreated);
        QVERIFY(manager.createBackup(true));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 10000);
        profile.close();
        settings.setScaleAddress(QStringLiteral("changed-after-backup"));
        AppSettings conversationSettings;
        conversationSettings.setValue(QStringLiteral("ai/conversations/index"),
            QByteArrayLiteral("[{\"key\":\"zip-existing\"}]"));
        conversationSettings.setValue(QStringLiteral("ai/conversations/zip-existing/messages"), QByteArrayLiteral("[]"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            QStringLiteral(".*SettingsSerializer.*importFromJson replacing.*favorites.*")));
        QVERIFY(manager.restoreBackupFile(created.first().first().toString(),
                                          false, true, true, true, true));
        QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 3, 10000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(settings.scaleAddress(), QStringLiteral("uploaded-zip-scale"));
        conversationSettings.sync();
        QVERIFY(!conversationSettings.contains(QStringLiteral("ai/conversations/index")));
        QVERIFY(!conversationSettings.contains(QStringLiteral("ai/conversations/zip-existing/messages")));
        QVERIFY(QFile::exists(profile.fileName()));
        QCOMPARE(media.getPersonalMediaList().size(), 2);
        for (const QVariant& item : media.getPersonalMediaList())
            QVERIFY(restoredNames.contains(item.toMap().value(QStringLiteral("filename")).toString()));
    }

    void operationsAndArgumentsAreClosed()
    {
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("createBackup")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("restoreBackup")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("setBackupHour")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("prepareBackupDownload")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("cancelFileOperation")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("readBundleChunk")));
        QVERIFY(BackupOperationPolicy::supported(QStringLiteral("releaseBundle")));
        QVERIFY(!BackupOperationPolicy::supported(QStringLiteral("openPath")));
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("createBackup"), {}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("createBackup"),
                                                        {{QStringLiteral("path"), QStringLiteral("/tmp/x")}}));
        const QString backupId = BackupOperationPolicy::backupIdForFilename(
            QStringLiteral("shots_backup_20261022.zip"));
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("prepareBackupDownload"),
            {{QStringLiteral("backupId"), backupId}}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("prepareBackupDownload"),
            {{QStringLiteral("backupId"), backupId}, {QStringLiteral("path"), QStringLiteral("/tmp/x")}}));
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("requestId"), QStringLiteral("pending-export")}}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("requestId"), QStringLiteral("pending-export")},
             {QStringLiteral("clientId"), QStringLiteral("spoofed")}}));
        const QString bundleId = QStringLiteral("12345678-1234-1234-1234-123456789abc");
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("readBundleChunk"),
            {{QStringLiteral("bundleId"), bundleId}, {QStringLiteral("offset"), 0}}));
        QVERIFY(!BackupOperationPolicy::validArguments(QStringLiteral("readBundleChunk"),
            {{QStringLiteral("bundleId"), bundleId},
             {QStringLiteral("offset"), FileBundlePolicy::MaximumBackupBytes + 1}}));
        QVERIFY(BackupOperationPolicy::validArguments(QStringLiteral("releaseBundle"),
            {{QStringLiteral("bundleId"), bundleId}}));
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
        QVERIFY(BackupOperationPolicy::downloadableBackupFilename(
            QStringLiteral("shots_backup_20261022.zip")));
        QVERIFY(!BackupOperationPolicy::downloadableBackupFilename(
            QStringLiteral("shots_backup_20261022.db")));
        QVERIFY(!BackupOperationPolicy::safeBackupFilename(QStringLiteral("shots_backup_20260231.zip")));
        QVERIFY(!BackupOperationPolicy::safeBackupFilename(QStringLiteral("../shots_backup_20261022.zip")));
        QVERIFY(!BackupOperationPolicy::safeBackupFilename(QStringLiteral("/Users/test/shots_backup_20261022.zip")));
        QVERIFY(!BackupOperationPolicy::validBackupId(QString(64, QLatin1Char('G'))));
        QVERIFY(!BackupOperationPolicy::validBackupId(QStringLiteral("shots_backup_20261022.zip")));
    }

    void backupBundleIsBoundToAdmittedClientAndReleasedOnClose()
    {
        NativeBackupServiceSource source(nullptr, nullptr);
        const QString owner = QStringLiteral("owner-client");
        const QString other = QStringLiteral("other-client");
        source.openClient(owner);
        source.openClient(other);

        auto directory = std::make_shared<QTemporaryDir>();
        QVERIFY(directory->isValid());
        const QString bundleId = QStringLiteral("12345678-1234-1234-1234-123456789abc");
        const QString fileName = bundleId + QStringLiteral(".zip");
        QFile file(directory->filePath(fileName));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("backup-bytes"), qint64(12));
        file.close();
        QVERIFY(!source.m_bundles.adopt(directory, bundleId, fileName,
            QStringLiteral("backup.zip"), FileBundlePolicy::MaximumBundleBytes,
            QStringLiteral("application/zip"), QStringLiteral("ZIP archives (*.zip)")).isEmpty());
        source.m_bundleClientId = owner;
        source.m_bundleId = bundleId;

        QSignalSpy completed(&source, &BackupStateSource::operationCompleted);
        source.execute(QStringLiteral("other-read"), QStringLiteral("readBundleChunk"),
            {{QStringLiteral("clientId"), other}, {QStringLiteral("bundleId"), bundleId},
             {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("invalidBundle"));
        QVERIFY(source.m_bundles.hasBundle());

        source.execute(QStringLiteral("owner-read"), QStringLiteral("readBundleChunk"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("bundleId"), bundleId},
             {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.size(), 2);
        QVERIFY(completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(3).toMap().value(QStringLiteral("data")).toByteArray(),
                 QByteArray("backup-bytes"));
        QVERIFY(source.snapshot().value(QStringLiteral("lastResult")).toMap().isEmpty());

        source.closeClient(owner);
        QVERIFY(!source.m_bundles.hasBundle());
        QVERIFY(source.m_bundleId.isEmpty());
        source.execute(QStringLiteral("closed-client-read"), QStringLiteral("readBundleChunk"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("bundleId"), bundleId},
             {QStringLiteral("offset"), 0}});
        QCOMPARE(completed.size(), 3);
        QVERIFY(!completed.last().at(2).toBool());
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("clientUnavailable"));
    }

    void pendingBackupExportCanOnlyBeCancelledByItsOwner()
    {
        NativeBackupServiceSource source(nullptr, nullptr);
        const QString owner = QStringLiteral("owner-client");
        const QString other = QStringLiteral("other-client");
        source.openClient(owner);
        source.openClient(other);
        const QString pendingId = QStringLiteral("pending-export");
        QCOMPARE(source.m_requests.begin(pendingId), ServiceRequestIds::Start::Accepted);
        source.m_activeId = pendingId;
        source.m_activeOperation = QStringLiteral("prepareBackupDownload");
        source.m_activeClientId = owner;
        QSignalSpy completed(&source, &BackupStateSource::operationCompleted);

        source.execute(QStringLiteral("other-cancel"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), other}, {QStringLiteral("requestId"), pendingId}});
        QCOMPARE(completed.size(), 1);
        QVERIFY(source.m_activeId == pendingId);
        QVERIFY(!completed.last().at(3).toMap().value(QStringLiteral("cancelled")).toBool());

        source.execute(QStringLiteral("owner-cancel"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("requestId"), pendingId}});
        QCOMPARE(completed.size(), 3);
        QCOMPARE(completed.at(1).at(0).toString(), pendingId);
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("prepareBackupDownload"));
        QVERIFY(!completed.at(1).at(2).toBool());
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("cancelled"));
        QCOMPARE(completed.at(2).at(0).toString(), QStringLiteral("owner-cancel"));
        QVERIFY(completed.at(2).at(2).toBool());
        QVERIFY(completed.at(2).at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        QVERIFY(source.m_activeId.isEmpty());
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(0));
    }

    void cancelledBackupCopyKeepsAdmissionUntilWorkerDrains()
    {
        DatabaseBackupManager manager(nullptr, nullptr);
        NativeBackupServiceSource source(&manager, nullptr);
        const QString owner = QStringLiteral("worker-owner");
        source.openClient(owner);
        const QString retiredId = QStringLiteral("retired-copy");
        QVERIFY(source.m_requests.begin(retiredId) == ServiceRequestIds::Start::Accepted);
        source.m_activeId = retiredId;
        source.m_activeOperation = QStringLiteral("prepareBackupDownload");
        source.m_activeClientId = owner;
        source.m_backupCopyInFlight = true;
        source.m_backupCopyRequestId = retiredId;
        source.m_backupCopyCancelled = std::make_shared<std::atomic_bool>(false);
        QSignalSpy completed(&source, &BackupStateSource::operationCompleted);

        source.execute(QStringLiteral("cancel-copy"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("requestId"), retiredId}});
        QCOMPARE(completed.size(), 2);
        QVERIFY(source.m_backupCopyCancelled->load(std::memory_order_relaxed));
        QVERIFY(source.m_backupCopyInFlight);
        QVERIFY(source.m_activeId.isEmpty());

        source.execute(QStringLiteral("replacement-copy"), QStringLiteral("prepareBackupDownload"),
            {{QStringLiteral("clientId"), owner},
             {QStringLiteral("backupId"), QString(64, QLatin1Char('a'))}});
        QCOMPARE(completed.size(), 3);
        QCOMPARE(completed.last().at(4).toString(), QStringLiteral("busy"));
        QVERIFY(source.m_backupCopyInFlight);

        auto directory = std::make_shared<QTemporaryDir>();
        QVERIFY(directory->isValid());
        source.backupDownloadPrepared(retiredId, owner,
            QStringLiteral("12345678-1234-1234-1234-123456789abc"),
            QStringLiteral("backup.zip"), directory, false, QStringLiteral("cancelled"));
        QVERIFY(!source.m_backupCopyInFlight);
        QVERIFY(!source.m_backupCopyCancelled);

        // A delayed callback from the retired worker cannot clear a later worker's slot.
        const QString newerId = QStringLiteral("newer-copy");
        source.m_backupCopyInFlight = true;
        source.m_backupCopyRequestId = newerId;
        source.m_backupCopyCancelled = std::make_shared<std::atomic_bool>(false);
        source.backupDownloadPrepared(retiredId, owner,
            QStringLiteral("22345678-1234-1234-1234-123456789abc"),
            QStringLiteral("backup.zip"), directory, false, QStringLiteral("cancelled"));
        QVERIFY(source.m_backupCopyInFlight);
        QCOMPARE(source.m_backupCopyRequestId, newerId);
        QVERIFY(source.m_backupCopyCancelled);
    }

    void preparedBackupBundleCanBeCancelledByItsOriginatingOwner()
    {
        NativeBackupServiceSource source(nullptr, nullptr);
        const QString owner = QStringLiteral("owner-client");
        const QString other = QStringLiteral("other-client");
        source.openClient(owner);
        source.openClient(other);

        const auto attachBundle = [&source](const QString& requestId, const QString& bundleId) {
            auto directory = std::make_shared<QTemporaryDir>();
            if (!directory->isValid()) return false;
            const QString fileName = bundleId + QStringLiteral(".zip");
            QFile file(directory->filePath(fileName));
            if (!file.open(QIODevice::WriteOnly) || file.write("prepared") != 8) return false;
            file.close();
            if (source.m_bundles.adopt(directory, bundleId, fileName,
                QStringLiteral("backup.zip"), FileBundlePolicy::MaximumBundleBytes,
                QStringLiteral("application/zip"), QStringLiteral("ZIP archives (*.zip)")).isEmpty())
                return false;
            source.m_bundleClientId = QStringLiteral("owner-client");
            source.m_bundleId = bundleId;
            source.m_bundleRequestId = requestId;
            return true;
        };

        const QString pendingRequest = QStringLiteral("prepared-before-reply");
        QCOMPARE(source.m_requests.begin(pendingRequest), ServiceRequestIds::Start::Accepted);
        QVERIFY(attachBundle(pendingRequest,
            QStringLiteral("12345678-1234-1234-1234-123456789abc")));
        QSignalSpy completed(&source, &BackupStateSource::operationCompleted);

        source.execute(QStringLiteral("wrong-owner-cancel"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), other}, {QStringLiteral("requestId"), pendingRequest}});
        QCOMPARE(completed.size(), 1);
        QVERIFY(source.m_bundles.hasBundle());
        QVERIFY(!completed.last().at(3).toMap().value(QStringLiteral("cancelled")).toBool());

        source.execute(QStringLiteral("origin-owner-cancel"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("requestId"), pendingRequest}});
        QCOMPARE(completed.size(), 3);
        QCOMPARE(completed.at(1).at(0).toString(), pendingRequest);
        QCOMPARE(completed.at(1).at(4).toString(), QStringLiteral("cancelled"));
        QVERIFY(completed.at(2).at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        QVERIFY(!source.m_bundles.hasBundle());
        QVERIFY(source.m_bundleRequestId.isEmpty());
        QCOMPARE(source.m_requests.pendingCount(), qsizetype(0));

        const QString completedRequest = QStringLiteral("prepared-after-reply");
        QVERIFY(attachBundle(completedRequest,
            QStringLiteral("22345678-1234-1234-1234-123456789abc")));
        QVERIFY(source.m_requests.begin(completedRequest) == ServiceRequestIds::Start::Accepted);
        source.m_requests.finish(completedRequest); // prepare completion was already delivered
        source.execute(QStringLiteral("cancel-after-reply"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("requestId"), completedRequest}});
        QCOMPARE(completed.size(), 4);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(completed.last().at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        QVERIFY(!source.m_bundles.hasBundle());
        QVERIFY(source.m_bundleRequestId.isEmpty());

        const QString expiredRequest = QStringLiteral("expired-prepared");
        QVERIFY(attachBundle(expiredRequest,
            QStringLiteral("32345678-1234-1234-1234-123456789abc")));
        QString releaseReason;
        QVERIFY(source.m_bundles.release(
            {{QStringLiteral("bundleId"), source.m_bundleId}}, &releaseReason));
        source.execute(QStringLiteral("observe-expiry"), QStringLiteral("cancelFileOperation"),
            {{QStringLiteral("clientId"), owner}, {QStringLiteral("requestId"), expiredRequest}});
        QCOMPARE(completed.size(), 5);
        QVERIFY(!completed.last().at(3).toMap().value(QStringLiteral("cancelled")).toBool());
        QVERIFY(source.m_bundleClientId.isEmpty());
        QVERIFY(source.m_bundleId.isEmpty());
        QVERIFY(source.m_bundleRequestId.isEmpty());
    }

    void existingManagerBackupIsCopiedAndReadInOpaqueChunks()
    {
        QTemporaryDir backupDirectory;
        QVERIFY(backupDirectory.isValid());
        const QString filename = QStringLiteral("shots_backup_")
            + QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))
            + QStringLiteral(".zip");
        const QString zipPath = backupDirectory.filePath(filename);
        const QByteArray payload(100000, 'z');
        {
            QuaZip writer(zipPath);
            QVERIFY(writer.open(QuaZip::mdCreate));
            QVERIFY(ZipArchive::addFile(writer, QStringLiteral("fixture.bin"), payload, 0, 0));
            writer.close();
            QCOMPARE(writer.getZipError(), UNZ_OK);
        }
        QFile original(zipPath);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray expectedZip = original.readAll();
        original.close();
        QVERIFY(expectedZip.size() > FileBundlePolicy::MaximumChunkBytes);

        DatabaseBackupManager manager(nullptr, nullptr);
        manager.m_backupDirectoryForTests = backupDirectory.path();
        manager.refreshBackupList();
        QCOMPARE(manager.availableBackups().size(), 1);

        NativeBackupServiceSource source(&manager, nullptr);
        const QString clientId = QStringLiteral("backup-download-client");
        source.openClient(clientId);
        QSignalSpy completed(&source, &BackupStateSource::operationCompleted);
        const QString prepareId = QStringLiteral("prepare-existing-backup");
        const QString backupId = BackupOperationPolicy::backupIdForFilename(filename);
        source.execute(prepareId, QStringLiteral("prepareBackupDownload"),
            {{QStringLiteral("clientId"), clientId}, {QStringLiteral("backupId"), backupId}});
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first().at(0).toString(), prepareId);
        QVERIFY(completed.first().at(2).toBool());
        const QVariantMap descriptor = completed.first().at(3).toMap();
        QCOMPARE(descriptor.value(QStringLiteral("displayName")).toString(), filename);
        QCOMPARE(descriptor.value(QStringLiteral("size")).toLongLong(), expectedZip.size());
        QCOMPARE(descriptor.value(QStringLiteral("mimeType")).toString(),
                 QStringLiteral("application/zip"));
        QVERIFY(!descriptor.contains(QStringLiteral("path")));
        QVERIFY(!descriptor.contains(QStringLiteral("data")));

        const QString bundleId = descriptor.value(QStringLiteral("bundleId")).toString();
        QVERIFY(FileBundlePolicy::validOpaqueId(bundleId));
        QByteArray received;
        int offset = 0;
        int chunkIndex = 0;
        while (offset < expectedZip.size()) {
            const QString chunkId = QStringLiteral("backup-chunk-%1").arg(chunkIndex++);
            source.execute(chunkId, QStringLiteral("readBundleChunk"),
                {{QStringLiteral("clientId"), clientId}, {QStringLiteral("bundleId"), bundleId},
                 {QStringLiteral("offset"), offset}});
            QCOMPARE(completed.size(), chunkIndex + 1);
            QVERIFY(completed.last().at(2).toBool());
            const QVariantMap chunk = completed.last().at(3).toMap();
            const QByteArray bytes = chunk.value(QStringLiteral("data")).toByteArray();
            QVERIFY(!bytes.isEmpty());
            QVERIFY(bytes.size() <= FileBundlePolicy::MaximumChunkBytes);
            received.append(bytes);
            offset = chunk.value(QStringLiteral("nextOffset")).toInt();
            QVERIFY(offset <= expectedZip.size());
        }
        QCOMPARE(received, expectedZip);

        source.execute(QStringLiteral("release-existing-backup"), QStringLiteral("releaseBundle"),
            {{QStringLiteral("clientId"), clientId}, {QStringLiteral("bundleId"), bundleId}});
        QCOMPARE(completed.size(), chunkIndex + 2);
        QVERIFY(completed.last().at(2).toBool());
        QVERIFY(!source.m_bundles.hasBundle());
        QVERIFY(source.m_bundleRequestId.isEmpty());
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
