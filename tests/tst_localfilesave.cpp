#include <QtTest>

#include "remote/localfileselection.h"
#include "remote/opaquefilebundlestore.h"
#include "remote/filebundlepolicy.h"

#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QApplication>
#include <QFontDatabase>
#include <QTemporaryDir>

struct LocalFileSelectionTestAccess
{
    static bool begin(LocalFileSelection& selection, const QString& requestId,
                      const QString& displayName, qint64 totalBytes, const QString& path,
                      const QString& mime = QStringLiteral("application/zip"),
                      const QString& filter = QStringLiteral("ZIP archives (*.zip)"))
    {
        return selection.beginSaveFileAtPath(requestId, displayName, totalBytes, path, mime, filter);
    }
    static bool download(LocalFileSelection& selection, const QString& requestId,
                         const QString& directory)
    {
        return selection.beginDownloadInDirectory(requestId, QStringLiteral("shot.json"), 2,
            directory, QStringLiteral("application/json"), QStringLiteral("JSON files (*.json)"));
    }
    static QFileDialog* dialog(LocalFileSelection& selection, const QString& requestId)
    {
        return selection.m_saveDialogs.value(requestId);
    }
};

struct OpaqueFileBundleStoreTestAccess
{
    static void expire(OpaqueFileBundleStore& store) { store.m_expiryTimer.start(0); }
    static void seek(OpaqueFileBundleStore& store, qint64 offset) { store.m_offset = offset; }
};

class tst_LocalFileSave final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void abortPreservesExistingDestination()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("profiles.zip"));
        {
            QFile original(path);
            QVERIFY(original.open(QIODevice::WriteOnly));
            QCOMPARE(original.write("previous archive"), qint64(16));
        }

        LocalFileSelection selection;
        QVERIFY(LocalFileSelectionTestAccess::begin(selection, QStringLiteral("request-a"),
                                                    QStringLiteral("profiles.zip"), 32, path));
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("request-a"), 0,
                                              QByteArrayLiteral("partial contents")));
        selection.cancelSaveFile(QStringLiteral("request-a"));

        QFile afterAbort(path);
        QVERIFY(afterAbort.open(QIODevice::ReadOnly));
        QCOMPARE(afterAbort.readAll(), QByteArrayLiteral("previous archive"));
    }

    void unwritableDestinationCanRetryWithoutRetainingAPartialSave()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalFileSelection selection;
        const QString request = QStringLiteral("recover-log-save");
        const QString displayName = QStringLiteral("debug.log");
        const QString mime = QStringLiteral("text/plain");
        const QString filter = QStringLiteral("Log files (*.log)");
        // A missing parent is unwritable even when the test runs with elevated
        // filesystem permissions; no platform-specific chmod assumptions.
        const QString unavailable = directory.filePath(QStringLiteral("missing/debug.log"));
        QVERIFY(!LocalFileSelectionTestAccess::begin(selection, request, displayName, 4,
                                                      unavailable, mime, filter));
        QVERIFY(!QFile::exists(unavailable));
        const QString destination = directory.filePath(displayName);
        QVERIFY(LocalFileSelectionTestAccess::begin(selection, request, displayName, 4,
                                                     destination, mime, filter));
        QVERIFY(selection.appendSaveFileChunk(request, 0, QByteArrayLiteral("log!")));
        QVERIFY(selection.finishSaveFile(request));
        QFile saved(destination);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArrayLiteral("log!"));
        QVERIFY(!selection.appendSaveFileChunk(request, 4, QByteArrayLiteral("late")));
    }

    void completedSaveAtomicallyReplacesDestination()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("profiles.zip"));
        {
            QFile original(path);
            QVERIFY(original.open(QIODevice::WriteOnly));
            QCOMPARE(original.write("old"), qint64(3));
        }

        LocalFileSelection selection;
        QVERIFY(LocalFileSelectionTestAccess::begin(selection, QStringLiteral("request-b"),
                                                    QStringLiteral("profiles.zip"), 4, path));
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("request-b"), 0,
                                              QByteArrayLiteral("new!")));
        QVERIFY(selection.finishSaveFile(QStringLiteral("request-b")));

        QFile afterCommit(path);
        QVERIFY(afterCommit.open(QIODevice::ReadOnly));
        QCOMPARE(afterCommit.readAll(), QByteArrayLiteral("new!"));
    }

    void jsonSaveUsesBoundedTypedMetadataAndAtomicCompletion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("translations-de.json"));
        LocalFileSelection selection;
        const auto beginJson = [&](qint64 size, const QString& mime, const QString& filter) {
            return LocalFileSelectionTestAccess::begin(selection, QStringLiteral("json-save"),
                QStringLiteral("translations-de.json"), size, path, mime, filter);
        };
        QVERIFY(!beginJson(2, QStringLiteral("text/html"), QStringLiteral("JSON files (*.json)")));
        QVERIFY(!beginJson(2, QStringLiteral("application/json"), QStringLiteral("All files (*)")));
        QVERIFY(beginJson(FileBundlePolicy::MaximumLanguageJsonBytes + 1,
            QStringLiteral("application/json"), QStringLiteral("JSON files (*.json)")));
        selection.cancelSaveFile(QStringLiteral("json-save"));
        QVERIFY(beginJson(2, QStringLiteral("application/json"), QStringLiteral("JSON files (*.json)")));
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("json-save"), 0, QByteArrayLiteral("{}")));
        QVERIFY(selection.finishSaveFile(QStringLiteral("json-save")));
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArrayLiteral("{}"));
    }

    void directDownloadPreservesExistingFilesAndCleansUpCancelledOutput()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile original(directory.filePath(QStringLiteral("shot.json")));
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("original"), qint64(8));
        original.close();
        LocalFileSelection selection;
        QVERIFY(LocalFileSelectionTestAccess::download(selection, QStringLiteral("download"), directory.path()));
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("download"), 0, QByteArrayLiteral("{}")));
        QVERIFY(selection.finishSaveFile(QStringLiteral("download")));
        QFile saved(directory.filePath(QStringLiteral("shot (1).json")));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArrayLiteral("{}"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArrayLiteral("original"));
        QVERIFY(LocalFileSelectionTestAccess::download(selection, QStringLiteral("cancelled"), directory.path()));
        selection.cancelSaveFile(QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(directory.filePath(QStringLiteral("shot (2).json"))));
    }

    void nativePickerIsDeferredAndSignalsReadinessBeforeWriting()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("translations.json"));
        LocalFileSelection selection;
        QSignalSpy ready(&selection, &LocalFileSelection::saveFileReady);
        QVERIFY(selection.beginSaveFile(QStringLiteral("picker"), QStringLiteral("translations.json"), 2,
            QStringLiteral("application/json"), QStringLiteral("JSON files (*.json)")));
        QCOMPARE(ready.size(), 0);
        QVERIFY(!selection.appendSaveFileChunk(QStringLiteral("picker"), 0, QByteArrayLiteral("{}")));
        auto* dialog = LocalFileSelectionTestAccess::dialog(selection, QStringLiteral("picker"));
        QVERIFY(dialog);
        QVERIFY(!dialog->isVisible());
        dialog->setOption(QFileDialog::DontUseNativeDialog);
        dialog->selectFile(path);
        // Resolve before queued open runs: the test exercises native selection
        // completion without displaying a dialog or entering a nested event loop.
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
        QCOMPARE(ready.size(), 0);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("picker"));
        QVERIFY(ready.first().at(1).toBool());
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("picker"), 0, QByteArrayLiteral("{}")));
        QVERIFY(selection.finishSaveFile(QStringLiteral("picker")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("{}"));
    }

    void conversationTextSaveUsesTheSharedFileSaver()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("conversation.txt"));
        LocalFileSelection selection;
        QVERIFY(LocalFileSelectionTestAccess::begin(selection, QStringLiteral("text"),
            QStringLiteral("conversation.txt"), 5, path, QStringLiteral("text/plain"),
            QStringLiteral("Text files (*.txt)")));
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("text"), 0, QByteArrayLiteral("chat\n")));
        QVERIFY(selection.finishSaveFile(QStringLiteral("text")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("chat\n"));
    }

    void plainLogSaveValidatesFormatAndWritesTheActualBytes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("debug.log"));
        LocalFileSelection selection;
        QVERIFY(!LocalFileSelectionTestAccess::begin(selection, QStringLiteral("log"),
            QStringLiteral("debug.log"), 4, path, QStringLiteral("text/html"),
            QStringLiteral("Log files (*.log)")));
        QVERIFY(!LocalFileSelectionTestAccess::begin(selection, QStringLiteral("large-log"),
            QStringLiteral("debug.log"), FileBundlePolicy::MaximumLogBytes + 1, path,
            QStringLiteral("text/plain"), QStringLiteral("Log files (*.log)")));
        QVERIFY(LocalFileSelectionTestAccess::begin(selection, QStringLiteral("log"),
            QStringLiteral("debug.log"), 4, path, QStringLiteral("text/plain"),
            QStringLiteral("Log files (*.log)")));
        QVERIFY(selection.appendSaveFileChunk(QStringLiteral("log"), 0, QByteArrayLiteral("log\n")));
        QVERIFY(selection.finishSaveFile(QStringLiteral("log")));
        QFile output(path);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArrayLiteral("log\n"));
    }

    void backupByteCountsCrossTwoGiBWithoutTruncation()
    {
        auto directory = std::make_shared<QTemporaryDir>();
        QVERIFY(directory->isValid());
        const QString id = QStringLiteral("a1234567-b123-c123-d123-e123456789ab");
        const QString path = directory->filePath(id + QStringLiteral(".zip"));
        const qint64 offset = 3LL * 1024 * 1024 * 1024;
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.seek(offset));
        QCOMPARE(file.write("tail"), qint64(4));
        file.close();
        OpaqueFileBundleStore store;
        const QVariantMap bundle = store.adopt(directory, id, id + QStringLiteral(".zip"),
            QStringLiteral("large-backup.zip"), FileBundlePolicy::MaximumBackupBytes,
            QStringLiteral("application/zip"), QStringLiteral("ZIP archives (*.zip)"));
        QVERIFY(!bundle.isEmpty());
        QCOMPARE(bundle.value(QStringLiteral("size")).toLongLong(), offset + 4);
        OpaqueFileBundleStoreTestAccess::seek(store, offset);
        QString reason;
        const QVariantMap chunk = store.readChunk({{QStringLiteral("bundleId"), id},
                                                   {QStringLiteral("offset"), offset}}, &reason);
        QVERIFY2(reason.isEmpty(), qPrintable(reason));
        QCOMPARE(chunk.value(QStringLiteral("data")).toByteArray(), QByteArrayLiteral("tail"));
        QCOMPARE(chunk.value(QStringLiteral("nextOffset")).toLongLong(), offset + 4);
        QVERIFY(chunk.value(QStringLiteral("complete")).toBool());
        qint64 parsed = 0;
        QVERIFY(ServiceValue::boundedInteger64(double(offset), 0, FileBundlePolicy::MaximumBackupBytes, &parsed));
        QCOMPARE(parsed, offset);
        QVERIFY(!ServiceValue::boundedInteger64(QString::number(offset), 0, FileBundlePolicy::MaximumBackupBytes, &parsed));
        QVERIFY(!ServiceValue::boundedInteger64(double(offset) + 0.5, 0, FileBundlePolicy::MaximumBackupBytes, &parsed));
        const QString savePath = directory->filePath(QStringLiteral("native-copy.zip"));
        LocalFileSelection selection;
        QVERIFY(LocalFileSelectionTestAccess::begin(selection, QStringLiteral("large-save"),
            QStringLiteral("large-backup.zip"), offset + 4, savePath));
        selection.cancelSaveFile(QStringLiteral("large-save"));
        QVERIFY(!QFile::exists(savePath));
    }

    void admittedLargeUploadUsesItsInspectedSizeAndRejectsLaterGrowth_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("filename");
        QTest::newRow("apk") << QStringLiteral("apkFile") << QStringLiteral("app.apk");
        QTest::newRow("backup") << QStringLiteral("backupArchive") << QStringLiteral("backup.dcbackup");
    }

    void admittedLargeUploadUsesItsInspectedSizeAndRejectsLaterGrowth()
    {
        QFETCH(QString, kind);
        QFETCH(QString, filename);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile input(directory.filePath(filename));
        QVERIFY(input.open(QIODevice::WriteOnly));
        const qint64 size = 64LL * 1024 * 1024 + 1;
        QVERIFY(input.resize(size));
        input.close();
        const QUrl url = QUrl::fromLocalFile(input.fileName());
        LocalFileSelection selection;
        QSignalSpy inspected(&selection, &LocalFileSelection::inspectionCompleted);
        QSignalSpy opened(&selection, &LocalFileSelection::fileOpened);
        selection.inspectFiles(QStringLiteral("large"), {url}, kind);
        QTRY_COMPARE(inspected.size(), 1);
        QVERIFY(inspected.first().at(1).toBool());
        selection.openSelectedFile(QStringLiteral("large"), 0, QStringLiteral("reader"));
        QTRY_COMPARE(opened.size(), 1);
        QVERIFY(opened.first().at(1).toBool());
        QCOMPARE(opened.first().at(3).toLongLong(), size);
        selection.closeFile(QStringLiteral("reader"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QVERIFY(input.resize(size + 1));
        input.close();
        selection.openSelectedFile(QStringLiteral("large"), 0, QStringLiteral("changed"));
        QTRY_COMPARE(opened.size(), 2);
        QVERIFY(!opened.last().at(1).toBool());
        selection.closeSelection(QStringLiteral("large"));
    }

    void cancelledNativePickerCannotCompleteOrBlockTheNextSave()
    {
        LocalFileSelection selection;
        QSignalSpy ready(&selection, &LocalFileSelection::saveFileReady);
        QVERIFY(selection.beginSaveFile(QStringLiteral("cancel-picker"), QStringLiteral("profiles.zip"), 2));
        QPointer<QFileDialog> cancelled = LocalFileSelectionTestAccess::dialog(selection, QStringLiteral("cancel-picker"));
        QVERIFY(cancelled);
        selection.cancelSaveFile(QStringLiteral("cancel-picker"));
        QVERIFY(!LocalFileSelectionTestAccess::dialog(selection, QStringLiteral("cancel-picker")));
        QVERIFY(selection.beginSaveFile(QStringLiteral("next-picker"), QStringLiteral("profiles.zip"), 2));
        auto* next = LocalFileSelectionTestAccess::dialog(selection, QStringLiteral("next-picker"));
        QVERIFY(next);
        QVERIFY(QMetaObject::invokeMethod(next, "reject", Qt::DirectConnection));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("next-picker"));
        QVERIFY(!ready.first().at(1).toBool());
        QCOMPARE(ready.first().at(2).toString(), QStringLiteral("saveCancelled"));
        QVERIFY(!selection.appendSaveFileChunk(QStringLiteral("cancel-picker"), 0, QByteArrayLiteral("{}")));
    }

    void requesterDestructionDeletesPendingNativePicker()
    {
        auto selection = std::make_unique<LocalFileSelection>();
        QVERIFY(selection->beginSaveFile(QStringLiteral("destroy-picker"), QStringLiteral("profiles.zip"), 2));
        QPointer<QFileDialog> dialog = LocalFileSelectionTestAccess::dialog(*selection, QStringLiteral("destroy-picker"));
        QVERIFY(dialog);
        selection.reset();
        QVERIFY(!dialog);
    }

    void cancellationAfterSelectionSuppressesQueuedSuccess()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalFileSelection selection;
        QSignalSpy ready(&selection, &LocalFileSelection::saveFileReady);
        QVERIFY(selection.beginSaveFile(QStringLiteral("selected-then-cancelled"), QStringLiteral("profiles.zip"), 2));
        QPointer<QFileDialog> dialog = LocalFileSelectionTestAccess::dialog(selection, QStringLiteral("selected-then-cancelled"));
        QVERIFY(dialog);
        dialog->setOption(QFileDialog::DontUseNativeDialog);
        dialog->selectFile(directory.filePath(QStringLiteral("profiles.zip")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
        selection.cancelSaveFile(QStringLiteral("selected-then-cancelled"));
        QVERIFY(!selection.appendSaveFileChunk(QStringLiteral("selected-then-cancelled"), 0, QByteArrayLiteral("{}")));
        QVERIFY(selection.beginSaveFile(QStringLiteral("following-save"), QStringLiteral("profiles.zip"), 2));
        auto* following = LocalFileSelectionTestAccess::dialog(selection, QStringLiteral("following-save"));
        QVERIFY(QMetaObject::invokeMethod(following, "reject", Qt::DirectConnection));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("following-save"));
        QVERIFY(!ready.first().at(1).toBool());
    }

    void languagePickerRequiresOneJsonWithinEightMiB()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("translations.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("{}"), qint64(2));
        file.close();
        LocalFileSelection selection;
        QSignalSpy inspected(&selection, &LocalFileSelection::inspectionCompleted);
        const QVariant url = QUrl::fromLocalFile(path);
        selection.inspectFiles(QStringLiteral("one"), {url}, QStringLiteral("languageJson"));
        QTRY_COMPARE(inspected.size(), 1);
        QVERIFY(inspected.last().at(1).toBool());
        QCOMPARE(inspected.last().at(2).toList().size(), 1);
        selection.closeSelection(QStringLiteral("one"));
        selection.inspectFiles(QStringLiteral("two"), {url, url}, QStringLiteral("languageJson"));
        QTRY_COMPARE(inspected.size(), 2);
        QVERIFY(!inspected.last().at(1).toBool());
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(FileBundlePolicy::MaximumLanguageJsonBytes + 1));
        file.close();
        selection.inspectFiles(QStringLiteral("large"), {url}, QStringLiteral("languageJson"));
        QTRY_COMPARE(inspected.size(), 3);
        QVERIFY(!inspected.last().at(1).toBool());
        QCOMPARE(inspected.last().at(3).toString(), QStringLiteral("fileSizeLimit"));
    }

    void opaqueBundleUsesSequentialChunksAndReleaseRemovesTemporaryOutput()
    {
        auto directory = std::make_shared<QTemporaryDir>();
        QVERIFY(directory->isValid());
        const QString id = QStringLiteral("a1234567-b123-c123-d123-e123456789ab");
        const QString path = directory->filePath(id + QStringLiteral(".json"));
        const QByteArray contents(FileBundlePolicy::MaximumChunkBytes + 3, 'x');
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(contents), contents.size());
        output.close();
        OpaqueFileBundleStore store;
        const QVariantMap descriptor = store.adopt(directory, id, id + QStringLiteral(".json"),
            QStringLiteral("shot.json"), ServiceValue::MaximumExactInteger,
            QStringLiteral("application/json"), QStringLiteral("JSON files (*.json)"));
        directory.reset();
        QCOMPARE(descriptor.size(), 5);
        QCOMPARE(descriptor.value(QStringLiteral("size")).toLongLong(), contents.size());
        QVERIFY(!descriptor.contains(QStringLiteral("path")));
        QString reason;
        const QVariantMap request{{QStringLiteral("bundleId"), id}, {QStringLiteral("offset"), 0}};
        QVariantMap invalid = request;
        invalid.insert(QStringLiteral("offset"), 0.5);
        QVERIFY(store.readChunk(invalid, &reason).isEmpty());
        QCOMPARE(reason, QStringLiteral("invalidOffset"));
        invalid = request;
        invalid.insert(QStringLiteral("bundleId"), QStringLiteral("../output.json"));
        QVERIFY(store.readChunk(invalid, &reason).isEmpty());
        QCOMPARE(reason, QStringLiteral("invalidBundle"));
        const QVariantMap first = store.readChunk(request, &reason);
        QVERIFY(reason.isEmpty());
        QCOMPARE(first.value(QStringLiteral("data")).toByteArray(), contents.left(FileBundlePolicy::MaximumChunkBytes));
        QVERIFY(!first.value(QStringLiteral("complete")).toBool());
        QVERIFY(store.readChunk(request, &reason).isEmpty()); // Old offsets never replay data.
        QCOMPARE(reason, QStringLiteral("invalidOffset"));
        const QVariantMap last = store.readChunk({{QStringLiteral("bundleId"), id},
            {QStringLiteral("offset"), FileBundlePolicy::MaximumChunkBytes}}, &reason);
        QVERIFY(reason.isEmpty());
        QCOMPARE(last.value(QStringLiteral("data")).toByteArray(), QByteArray(3, 'x'));
        QVERIFY(last.value(QStringLiteral("complete")).toBool());
        QVERIFY(store.release({{QStringLiteral("bundleId"), id}}, &reason));
        QVERIFY(reason.isEmpty());
        QVERIFY(!store.hasBundle());
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!store.release({{QStringLiteral("bundleId"), id}}, &reason));
    }

    void opaqueZipPreservesMetadataAndIdleExpiryInvalidatesItsIdentity()
    {
        auto directory = std::make_shared<QTemporaryDir>();
        QVERIFY(directory->isValid());
        const QString id = QStringLiteral("a1234567-b123-c123-d123-e123456789ab");
        const QString path = directory->filePath(id + QStringLiteral(".zip"));
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write("zip"), qint64(3));
        output.close();
        OpaqueFileBundleStore store;
        const QVariantMap descriptor = store.adopt(directory, id, id + QStringLiteral(".zip"),
            QStringLiteral("converted-profiles.zip"), FileBundlePolicy::MaximumBundleBytes,
            QStringLiteral("application/zip"), QStringLiteral("ZIP archives (*.zip)"));
        QVERIFY(!descriptor.isEmpty());
        QCOMPARE(descriptor.value(QStringLiteral("mimeType")).toString(), QStringLiteral("application/zip"));
        QVERIFY(store.adopt(directory, id, id + QStringLiteral(".zip"), QStringLiteral("another.zip"),
            FileBundlePolicy::MaximumBundleBytes, QStringLiteral("application/zip"),
            QStringLiteral("ZIP archives (*.zip)")).isEmpty());
        directory.reset();
        OpaqueFileBundleStoreTestAccess::expire(store);
        QTRY_VERIFY(!store.hasBundle());
        QVERIFY(!QFileInfo::exists(path));
        QString reason;
        QVERIFY(store.readChunk({{QStringLiteral("bundleId"), id}, {QStringLiteral("offset"), 0}}, &reason).isEmpty());
        QCOMPARE(reason, QStringLiteral("invalidBundle"));
    }
};

int main(int argc, char* argv[])
{
    // The shared QtTest PCH is parsed without QtWidgets, so QTEST_MAIN's
    // application choice is already fixed there (Qt 6.12 qtest.h:282-296).
    QApplication app(argc, argv);
    const QStringList installedFamilies = QFontDatabase::families();
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (!installedFamilies.contains(font.family())) {
        const QStringList sansFamilies{QStringLiteral("Arial"), QStringLiteral("DejaVu Sans"),
            QStringLiteral("Liberation Sans"), QStringLiteral("Noto Sans")};
        QString family;
        for (const QString& candidate : sansFamilies) {
            if (installedFamilies.contains(candidate)) { family = candidate; break; }
        }
        if (family.isEmpty()) family = installedFamilies.value(0);
        if (family.isEmpty()) {
            qCritical("The file-picker fixture requires an installed font family.");
            return 1;
        }
        font.setFamily(family);
    }
    QApplication::setFont(font);
    tst_LocalFileSave test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_localfilesave.moc"
