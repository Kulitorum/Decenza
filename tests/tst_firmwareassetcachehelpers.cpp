#include <QtTest>
#include <QHash>
#include <QTemporaryDir>

#include "core/firmwareassetcache.h"

class tst_FirmwareAssetCacheHelpers : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void manifestContainsExpectedEntriesAndReleaseNotes() {
        QFile manifest(QString::fromLatin1(
            DE1::Firmware::FirmwareAssetCache::FIRMWARE_MANIFEST_RESOURCE));
        QVERIFY2(manifest.open(QIODevice::ReadOnly),
                 qPrintable(QStringLiteral("manifest missing: %1").arg(manifest.errorString())));

        QString error;
        const auto entries = DE1::Firmware::parseFirmwareManifest(manifest.readAll(), &error);
        QVERIFY2(!entries.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 2);

        QHash<QString, DE1::Firmware::FirmwareCatalogEntry> byId;
        for (const auto& entry : entries) {
            byId.insert(entry.id, entry);
        }
        QVERIFY(byId.contains(QStringLiteral("de1-1352")));
        QVERIFY(byId.contains(QStringLiteral("de1-1358")));

        const auto stable = byId.value(QStringLiteral("de1-1352"));
        QCOMPARE(stable.build, uint32_t(1352));
        QCOMPARE(stable.byteLength, qint64(463872));
        QCOMPARE(stable.sha256Hex,
                 QByteArray("d9433b85167566d7b457e03e2151e860c10ff5d3b4e41b163667b8314aeb2927"));
        QCOMPARE(stable.expectedHeaderBoardMarker, uint32_t(0xDE100001));
        QCOMPARE(stable.expectedBodyByteCount, uint32_t(461824));
        QCOMPARE(stable.expectedCpuByteCount, uint32_t(298592));
        QVERIFY(stable.releaseNotes.contains(QStringLiteral("NoAC")));

        const auto early = byId.value(QStringLiteral("de1-1358"));
        QCOMPARE(early.build, uint32_t(1358));
        QCOMPARE(early.byteLength, qint64(463872));
        QCOMPARE(early.sha256Hex,
                 QByteArray("ada25161ebbd661b44b3aab2c7756f42d95064a931462b3d134e8db66d198747"));
        QCOMPARE(early.expectedHeaderBoardMarker, uint32_t(0xDE100001));
        QCOMPARE(early.expectedBodyByteCount, uint32_t(461824));
        QCOMPARE(early.expectedCpuByteCount, uint32_t(298880));
        QVERIFY(early.releaseNotes.contains(QStringLiteral("Cold maintenance")));
    }

    void bundledFilesValidateAgainstManifest() {
        QFile manifest(QString::fromLatin1(
            DE1::Firmware::FirmwareAssetCache::FIRMWARE_MANIFEST_RESOURCE));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        QString error;
        const auto entries = DE1::Firmware::parseFirmwareManifest(manifest.readAll(), &error);
        QVERIFY2(!entries.isEmpty(), qPrintable(error));

        for (const auto& entry : entries) {
            const auto result = DE1::Firmware::validateBundledFirmwareFile(
                entry.resourcePath(), entry);
            QCOMPARE(result.status, DE1::Firmware::Validation::Ok);
            QCOMPARE(result.header.version, entry.build);
            QCOMPARE(result.header.boardMarker, entry.expectedHeaderBoardMarker);
            QCOMPARE(result.header.byteCount, entry.expectedBodyByteCount);
            QCOMPARE(result.header.cpuBytes, entry.expectedCpuByteCount);
        }
    }

    void channelSelectionClassifiesAndExposesReleaseNotes() {
        DE1::Firmware::FirmwareAssetCache cache;

        QList<DE1::Firmware::FirmwareAssetCache::CheckResult> results;
        connect(&cache, &DE1::Firmware::FirmwareAssetCache::checkFinished,
                this, [&results](DE1::Firmware::FirmwareAssetCache::CheckResult r) {
                    results.append(r);
                });

        cache.checkForUpdate(/*installed*/ 1200);
        QCOMPARE(results.size(), 1);
        auto stable = results.takeFirst();
        QCOMPARE(stable.kind, DE1::Firmware::FirmwareAssetCache::CheckResult::Newer);
        QCOMPARE(stable.remoteVersion, uint32_t(1352));
        QCOMPARE(stable.channelLabel, QStringLiteral("Stable"));
        QVERIFY(stable.releaseNotes.contains(QStringLiteral("NoAC")));

        cache.setChannel(DE1::Firmware::FirmwareAssetCache::Channel::EarlyAccess);
        cache.checkForUpdate(/*installed*/ 1358);
        QCOMPARE(results.size(), 1);
        auto early = results.takeFirst();
        QCOMPARE(early.kind, DE1::Firmware::FirmwareAssetCache::CheckResult::Same);
        QCOMPARE(early.remoteVersion, uint32_t(1358));
        QCOMPARE(early.channelLabel, QStringLiteral("Early access"));
        QVERIFY(early.releaseNotes.contains(QStringLiteral("Cold maintenance")));

        cache.checkForUpdate(/*installed*/ 2000);
        QCOMPARE(results.size(), 1);
        auto downgrade = results.takeFirst();
        QCOMPARE(downgrade.kind, DE1::Firmware::FirmwareAssetCache::CheckResult::Older);
        QCOMPARE(downgrade.remoteVersion, uint32_t(1358));
    }

    void invalidManifestRejected() {
        QString error;
        const auto entries = DE1::Firmware::parseFirmwareManifest(
            QByteArray(R"({"schemaVersion":1,"artifacts":[{"id":"de1-1352"}]})"),
            &error);
        QVERIFY(entries.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void bundledValidationRejectsMismatchedFile() {
        QFile manifest(QString::fromLatin1(
            DE1::Firmware::FirmwareAssetCache::FIRMWARE_MANIFEST_RESOURCE));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        QString error;
        const auto entries = DE1::Firmware::parseFirmwareManifest(manifest.readAll(), &error);
        QVERIFY2(!entries.isEmpty(), qPrintable(error));
        const auto entry = entries.first();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("de1-1352.bin"));
        QFile corrupt(path);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write(QByteArray(128, char(0)));
        corrupt.close();

        const auto result = DE1::Firmware::validateBundledFirmwareFile(path, entry);
        QVERIFY(result.status != DE1::Firmware::Validation::Ok);
    }
};

QTEST_GUILESS_MAIN(tst_FirmwareAssetCacheHelpers)
#include "tst_firmwareassetcachehelpers.moc"
