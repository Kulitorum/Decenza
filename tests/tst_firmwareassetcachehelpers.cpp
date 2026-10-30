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

        // Selection picks the first entry per channel, so a rotation that
        // leaves two builds on one channel would silently ship the wrong one.
        QHash<QString, DE1::Firmware::FirmwareCatalogEntry> byChannel;
        for (const auto& entry : entries) {
            QVERIFY2(!byChannel.contains(entry.channel), qPrintable(entry.channel));
            byChannel.insert(entry.channel, entry);
        }
        QVERIFY(byChannel.contains(QStringLiteral("stable")));
        QVERIFY(byChannel.contains(QStringLiteral("early-access")));

        const auto stable = byChannel.value(QStringLiteral("stable"));
        QCOMPARE(stable.id, QStringLiteral("de1-1358"));
        QCOMPARE(stable.build, uint32_t(1358));
        QCOMPARE(stable.byteLength, qint64(463872));
        QCOMPARE(stable.sha256Hex,
                 QByteArray("ada25161ebbd661b44b3aab2c7756f42d95064a931462b3d134e8db66d198747"));
        QCOMPARE(stable.expectedHeaderBoardMarker, uint32_t(0xDE100001));
        QCOMPARE(stable.expectedBodyByteCount, uint32_t(461824));
        QCOMPARE(stable.expectedCpuByteCount, uint32_t(298880));
        QVERIFY(stable.releaseNotes.contains(QStringLiteral("Cold maintenance")));

        const auto early = byChannel.value(QStringLiteral("early-access"));
        QCOMPARE(early.id, QStringLiteral("de1-1359"));
        QCOMPARE(early.build, uint32_t(1359));
        QCOMPARE(early.byteLength, qint64(464896));
        QCOMPARE(early.sha256Hex,
                 QByteArray("4b9ed1c16ee4c015052765505404a4c5fb54a6c4e20063faf44b4b06dbd1cc81"));
        QCOMPARE(early.expectedHeaderBoardMarker, uint32_t(0xDE100001));
        QCOMPARE(early.expectedBodyByteCount, uint32_t(462848));
        QCOMPARE(early.expectedCpuByteCount, uint32_t(299584));
        QVERIFY(early.releaseNotes.contains(QStringLiteral("hot water")));
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
        QCOMPARE(stable.remoteVersion, uint32_t(1358));
        QCOMPARE(stable.channelLabel, QStringLiteral("Stable"));
        QVERIFY(stable.releaseNotes.contains(QStringLiteral("Cold maintenance")));

        cache.setChannel(DE1::Firmware::FirmwareAssetCache::Channel::EarlyAccess);
        cache.checkForUpdate(/*installed*/ 1359);
        QCOMPARE(results.size(), 1);
        auto early = results.takeFirst();
        QCOMPARE(early.kind, DE1::Firmware::FirmwareAssetCache::CheckResult::Same);
        QCOMPARE(early.remoteVersion, uint32_t(1359));
        QCOMPARE(early.channelLabel, QStringLiteral("Early access"));
        QVERIFY(early.releaseNotes.contains(QStringLiteral("hot water")));

        cache.checkForUpdate(/*installed*/ 2000);
        QCOMPARE(results.size(), 1);
        auto downgrade = results.takeFirst();
        QCOMPARE(downgrade.kind, DE1::Firmware::FirmwareAssetCache::CheckResult::Older);
        QCOMPARE(downgrade.remoteVersion, uint32_t(1359));
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
