#include "importservicesource.h"
#include "core/diagnosticlogging.h"
#include "filebundlepolicy.h"

#include "history/shotimporter.h"
#include "history/shothistorystorage.h"
#include "profile/profileimporter.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <utility>

namespace {
constexpr qsizetype MaxChunkBytes = 48 * 1024;
constexpr int TransferIdleTimeoutMs = 5 * 60 * 1000;
}

NativeImportServiceSource::NativeImportServiceSource(ShotImporter* shots,
                                                     ProfileImporter* profiles,
                                                     ShotHistoryStorage* storage,
                                                     QObject* parent)
    : ImportStateSimpleSource(parent), m_shots(shots), m_profiles(profiles), m_storage(storage)
{
    m_transferExpiry.setSingleShot(true);
    connect(&m_transferExpiry, &QTimer::timeout, this, [this] {
        if (m_transfer) {
            m_transfer.reset();
            refreshSnapshot();
        }
    });
    if (m_shots) {
        connect(m_shots, &ShotImporter::importComplete, this,
                [this](int imported, int skipped, int failed) {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("commitTransfer")
                || m_activeOperation == QLatin1String("importFromDE1App")) {
                m_deviceImportResult.insert(QStringLiteral("shotsImported"), imported);
                m_deviceImportResult.insert(QStringLiteral("shotsSkipped"), skipped);
                m_deviceImportResult.insert(QStringLiteral("shotsFailed"), failed);
                m_waitingForShotImport = false;
                maybeFinishDeviceImport();
            }
        });
        connect(m_shots, &ShotImporter::importError, this,
                [this](const QString& key, const QString& message) {
            Q_UNUSED(message)
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("commitTransfer")
                || m_activeOperation == QLatin1String("importFromDE1App")) {
                if (m_activeOperation == QLatin1String("importFromDE1App"))
                    m_deviceImportResult.insert(QStringLiteral("shotsError"), key.left(128));
                else
                    m_deviceImportError = key.left(128);
                m_waitingForShotImport = false;
                maybeFinishDeviceImport();
            }
        });
        connect(m_shots, &ShotImporter::progressChanged, this, [this] {
            refreshSnapshot();
            if (!m_activeRequestId.isEmpty())
                emit operationProgress(m_activeRequestId, makeSnapshot());
        });
        connect(m_shots, &ShotImporter::statusMessageChanged, this, &NativeImportServiceSource::refreshSnapshot);
        connect(m_shots, &ShotImporter::isImportingChanged, this, &NativeImportServiceSource::refreshSnapshot);
        connect(m_shots, &ShotImporter::isExtractingChanged, this, &NativeImportServiceSource::refreshSnapshot);
    }
    if (m_profiles) {
        connect(m_profiles, &ProfileImporter::batchImportComplete, this,
                [this](int imported, int skipped, int failed) {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("importFromDE1App")) {
                m_deviceImportResult.insert(QStringLiteral("profilesImported"), imported);
                m_deviceImportResult.insert(QStringLiteral("profilesSkipped"), skipped);
                m_deviceImportResult.insert(QStringLiteral("profilesFailed"), failed);
                m_waitingForProfileImport = false;
                maybeFinishDeviceImport();
            }
        });
        connect(m_profiles, &ProfileImporter::importFailed, this, [this](const QString& error) {
            refreshSnapshot();
            Q_UNUSED(error)
        });
        connect(m_profiles, &ProfileImporter::progressChanged, this, [this] {
            refreshSnapshot();
            if (!m_activeRequestId.isEmpty())
                emit operationProgress(m_activeRequestId, makeSnapshot());
        });
        connect(m_profiles, &ProfileImporter::statusMessageChanged, this, &NativeImportServiceSource::refreshSnapshot);
        connect(m_profiles, &ProfileImporter::isImportingChanged, this, &NativeImportServiceSource::refreshSnapshot);
        connect(m_profiles, &ProfileImporter::isScanningChanged, this, &NativeImportServiceSource::refreshSnapshot);
    }
    if (m_storage) {
        connect(m_storage, &ShotHistoryStorage::importDatabaseFinished, this, [this](bool success) {
            // The storage completion signal is global. A competing request is
            // rejected synchronously while the real import's atomic busy flag
            // remains set; do not let that rejection finish this operation.
            if (m_activeOperation != QLatin1String("commitTransfer")
                || !m_waitingForDatabaseImport || m_storage->isImportInProgress())
                return;
            m_waitingForDatabaseImport = false;
            m_deviceImportResult.insert(QStringLiteral("databaseImported"), success);
            if (success) m_deviceImportResult.insert(QStringLiteral("totalShots"), m_storage->totalShots());
            if (!success) m_deviceImportError = QStringLiteral("databaseImportFailed");
            maybeFinishDeviceImport();
        });
    }
    refresh();
}

QVariantMap NativeImportServiceSource::makeSnapshot() const
{
    const QString shotStatus = m_shots && m_shots->isExtracting()
        ? QStringLiteral("Extracting archive...")
        : (m_shots && m_shots->isImporting() ? QStringLiteral("Importing shots...") : QString());
    const QString profileStatus = m_profiles && m_profiles->isScanning()
        ? QStringLiteral("Scanning profiles...")
        : (m_profiles && m_profiles->isImporting() ? QStringLiteral("Importing profiles...") : QString());
    return {{QStringLiteral("shotDetected"), m_shotDetected},
            {QStringLiteral("profileDetected"), m_profileDetected},
            {QStringLiteral("shotImporting"), m_shots && m_shots->isImporting()},
            {QStringLiteral("shotExtracting"), m_shots && m_shots->isExtracting()},
            {QStringLiteral("shotTotalFiles"), m_shots ? m_shots->totalFiles() : 0},
            {QStringLiteral("shotProcessedFiles"), m_shots ? m_shots->processedFiles() : 0},
            {QStringLiteral("shotStatus"), shotStatus},
            {QStringLiteral("profileScanning"), m_profiles && m_profiles->isScanning()},
            {QStringLiteral("profileImporting"), m_profiles && m_profiles->isImporting()},
            {QStringLiteral("profileTotal"), m_profiles ? m_profiles->totalProfiles() : 0},
            {QStringLiteral("profileProcessed"), m_profiles ? m_profiles->processedProfiles() : 0},
            {QStringLiteral("profileStatus"), profileStatus},
            {QStringLiteral("busy"), !m_activeRequestId.isEmpty() || bool(m_transfer)}};
}

void NativeImportServiceSource::refreshSnapshot()
{
    setSnapshot(makeSnapshot());
    setReady(m_shots && m_profiles && m_storage);
}

void NativeImportServiceSource::refresh()
{
    m_shotDetected = m_shots && !m_shots->detectDE1AppHistoryPath().isEmpty();
    m_profileDetected = m_profiles && !m_profiles->detectDE1AppPath().isEmpty();
    refreshSnapshot();
}

QString NativeImportServiceSource::reasonFor(ServiceRequestIds::Start start) const
{
    if (start == ServiceRequestIds::Start::InvalidId) return QStringLiteral("invalidRequest");
    if (start == ServiceRequestIds::Start::DuplicateId) return QStringLiteral("duplicateRequest");
    if (start == ServiceRequestIds::Start::LimitReached) return QStringLiteral("tooManyRequests");
    return {};
}

std::unique_ptr<QTemporaryDir> NativeImportServiceSource::takeCompletedTransfer(
    const QString& transferId, const QString& expectedKind)
{
    if (!m_transfer || !m_transfer->completed || m_transfer->id != transferId
        || m_transfer->kind != expectedKind)
        return {};
    for (const TransferFile& file : std::as_const(m_transfer->files))
        if (file.offset != file.expectedSize)
            return {};
    auto directory = std::move(m_transfer->directory);
    m_transfer.reset();
    m_transferExpiry.stop();
    refreshSnapshot();
    return directory;
}

void NativeImportServiceSource::finish(const QString& requestId, const QString& operation,
                                       bool success, const QVariantMap& result,
                                       const QString& reason)
{
    if (!success)
        DIAG_DEBUG(APP, "ImportService") << "Operation refused:" << operation << reason;
    m_requests.finish(requestId);
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeImportServiceSource::finishActive(bool success, const QVariantMap& result,
                                              const QString& reason)
{
    if (m_activeRequestId.isEmpty()) return;
    const QString id = m_activeRequestId;
    const QString operation = m_activeOperation;
    m_activeRequestId.clear();
    m_activeOperation.clear();
    m_waitingForShotImport = false;
    m_waitingForProfileImport = false;
    m_waitingForDatabaseImport = false;
    m_activeTransfer.reset();
    refreshSnapshot();
    finish(id, operation, success, result, reason);
}

void NativeImportServiceSource::maybeFinishDeviceImport()
{
    if (m_waitingForShotImport || m_waitingForProfileImport || m_waitingForDatabaseImport) return;
    const bool success = m_deviceImportError.isEmpty();
    const QString error = m_deviceImportError;
    m_deviceImportError.clear();
    const QVariantMap result = m_deviceImportResult;
    m_deviceImportResult.clear();
    finishActive(success, result, error);
}

void NativeImportServiceSource::execute(QString requestId, QString operation,
                                        QVariantMap arguments)
{
    const ServiceRequestIds::Start begun = m_requests.begin(requestId);
    if (begun == ServiceRequestIds::Start::DuplicateId) return;
    if (begun != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false, {}, reasonFor(begun));
        return;
    }
    const bool interrupt = operation == QLatin1String("cancel");
    if (!m_shots || !m_profiles) {
        finish(requestId, operation, false, {}, QStringLiteral("unavailable"));
        return;
    }
    if (!m_activeRequestId.isEmpty() && !interrupt) {
        finish(requestId, operation, false, {}, QStringLiteral("busy"));
        return;
    }
    if (interrupt) {
        bool transferCancelled = false;
        if (m_transfer && (arguments.value(QStringLiteral("transferId")).toString().isEmpty()
                           || arguments.value(QStringLiteral("transferId")).toString() == m_transfer->id)) {
            m_transfer.reset();
            m_transferExpiry.stop();
            transferCancelled = true;
        }
        if (m_waitingForDatabaseImport) {
            finish(requestId, operation, false, {}, QStringLiteral("notCancellable"));
            return;
        }
        // Match the existing shared History UI: cancellation targets the shot
        // importer. ProfileImporter exposes no batch-cancel completion and
        // must be allowed to finish its already-started scan/import.
        m_shots->cancel();
        if (transferCancelled) refreshSnapshot();
        finish(requestId, operation, true);
        return;
    }

    if (operation == QLatin1String("beginTransfer")) {
        if (m_transfer) { finish(requestId, operation, false, {}, QStringLiteral("transferBusy")); return; }
        const QString kind = arguments.value(QStringLiteral("kind")).toString();
        const QVariantList files = arguments.value(QStringLiteral("files")).toList();
        if (files.isEmpty() || files.size() > FileBundlePolicy::maximumUploadFiles(kind)) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidSelection")); return;
        }
        auto transfer = std::make_unique<Transfer>();
        transfer->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        transfer->beginRequestId = requestId;
        transfer->kind = kind;
        transfer->overwrite = arguments.value(QStringLiteral("overwrite")).toBool();
        transfer->merge = arguments.value(QStringLiteral("merge")).toBool();
        transfer->directory = std::make_unique<QTemporaryDir>();
        if (!transfer->directory->isValid()) {
            finish(requestId, operation, false, {}, QStringLiteral("temporaryStorageUnavailable")); return;
        }
        for (qsizetype i = 0; i < files.size(); ++i) {
            const QVariantMap file = files.at(i).toMap();
            const QString name = file.value(QStringLiteral("name")).toString();
            qint64 size = 0;
            if (!FileBundlePolicy::validUploadFilename(kind, name)
                || !ServiceValue::boundedInteger64(file.value(QStringLiteral("size")), 1,
                    FileBundlePolicy::maximumUploadFileBytes(kind), &size)) {
                finish(requestId, operation, false, {}, QStringLiteral("invalidFileMetadata")); return;
            }
            transfer->totalBytes += size;
            if (transfer->totalBytes > FileBundlePolicy::maximumUploadBytes(kind)) {
                finish(requestId, operation, false, {}, QStringLiteral("transferSizeLimit")); return;
            }
            const QString path = transfer->directory->filePath(QString::number(i) + QLatin1Char('_') + name);
            transfer->files.append({name, path, size, 0});
        }
        const QString ticket = transfer->id;
        m_transfer = std::move(transfer);
        m_transferExpiry.start(TransferIdleTimeoutMs);
        refreshSnapshot();
        finish(requestId, operation, true,
               {{QStringLiteral("transferId"), ticket}, {QStringLiteral("chunkBytes"), MaxChunkBytes}});
        return;
    }

    if (operation == QLatin1String("appendTransferChunk")) {
        if (!m_transfer || arguments.value(QStringLiteral("transferId")).toString() != m_transfer->id) {
            finish(requestId, operation, false, {}, QStringLiteral("unknownTransfer")); return;
        }
        const int index = arguments.value(QStringLiteral("fileIndex")).toInt();
        const qint64 offset = arguments.value(QStringLiteral("offset")).toLongLong();
        const QByteArray data = arguments.value(QStringLiteral("data")).toByteArray();
        if (index < 0 || index >= m_transfer->files.size() || data.isEmpty()
            || data.size() > MaxChunkBytes) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidChunk")); return;
        }
        TransferFile& file = m_transfer->files[index];
        if (offset != file.offset || offset + data.size() > file.expectedSize) {
            finish(requestId, operation, false, {}, QStringLiteral("invalidOffset")); return;
        }
        QFile output(file.path);
        const QIODevice::OpenMode mode = file.offset == 0 ? QIODevice::WriteOnly : QIODevice::Append;
        if (!output.open(mode) || output.write(data) != data.size()) {
            finish(requestId, operation, false, {}, QStringLiteral("writeFailed")); return;
        }
        file.offset += data.size();
        m_transferExpiry.start(TransferIdleTimeoutMs);
        finish(requestId, operation, true,
               {{QStringLiteral("nextOffset"), file.offset},
                {QStringLiteral("fileComplete"), file.offset == file.expectedSize}});
        return;
    }

    if (operation == QLatin1String("commitTransfer")) {
        const QString ticket = arguments.value(QStringLiteral("transferId")).toString();
        if (!m_transfer || ticket != m_transfer->id) {
            finish(requestId, operation, false, {}, QStringLiteral("unknownTransfer")); return;
        }
        for (const TransferFile& file : std::as_const(m_transfer->files)) {
            if (file.offset != file.expectedSize) {
                finish(requestId, operation, false, {}, QStringLiteral("incompleteTransfer")); return;
            }
        }
        if (m_transfer->kind == QLatin1String("tclFiles")
            || m_transfer->kind == QLatin1String("profileFiles")
            || m_transfer->kind == QLatin1String("languageJson")
            || m_transfer->kind == QLatin1String("apkFile")
            || m_transfer->kind == QLatin1String("backupArchive")
            || m_transfer->kind == QLatin1String("personalMediaFile")) {
            m_transfer->completed = true;
            m_transferExpiry.start(TransferIdleTimeoutMs);
            finish(requestId, operation, true,
                   {{QStringLiteral("transferId"), m_transfer->id}});
            return;
        }
        const QString kind = m_transfer->kind;
        const bool overwrite = m_transfer->overwrite;
        const bool merge = m_transfer->merge;
        const QString directoryPath = m_transfer->directory->path();
        const QString filePath = m_transfer->files.first().path;
        m_activeTransfer = std::move(m_transfer);
        m_transferExpiry.stop();
        m_activeRequestId = requestId;
        m_activeOperation = operation;
        m_deviceImportResult.clear();
        m_deviceImportError.clear();
        if (kind == QLatin1String("database")) {
            if (!m_storage || m_storage->isImportInProgress()) {
                m_deviceImportError = QStringLiteral("databaseImportBusy");
                finishActive(false, {}, m_deviceImportError);
                return;
            }
            m_waitingForDatabaseImport = true;
            m_storage->requestImportDatabase(filePath, merge);
        } else {
            if (m_shots->isImporting() || m_shots->isExtracting()) {
                m_activeTransfer.reset();
                finishActive(false, {}, QStringLiteral("importBusy"));
                return;
            }
            m_waitingForShotImport = true;
            if (kind == QLatin1String("shotZip")) m_shots->importFromZip(filePath, overwrite);
            else m_shots->importFromDirectory(directoryPath, overwrite);
        }
        refreshSnapshot();
        return;
    }

    if (operation == QLatin1String("importFromDE1App")) {
        if (m_shots->isImporting() || m_shots->isExtracting()
            || m_profiles->isScanning() || m_profiles->isImporting()) {
            finish(requestId, operation, false, {}, QStringLiteral("importBusy"));
            return;
        }
        m_activeRequestId = requestId;
        m_activeOperation = operation;
        m_waitingForShotImport = true;
        m_waitingForProfileImport = true;
        m_deviceImportResult.clear();
        m_deviceImportError.clear();
        const bool overwrite = arguments.value(QStringLiteral("overwrite")).toBool();
        m_shots->importFromDE1App(overwrite);
        m_profiles->importFromDE1App(overwrite);
        refreshSnapshot();
        return;
    }

    if (operation == QLatin1String("cancelTransfer")) {
        const QString id = arguments.value(QStringLiteral("transferId")).toString();
        const QString beginId = arguments.value(QStringLiteral("requestId")).toString();
        if (m_transfer && (arguments.contains(QStringLiteral("requestId"))
                ? !beginId.isEmpty() && beginId == m_transfer->beginRequestId
                : id.isEmpty() || id == m_transfer->id))
            m_transfer.reset();
        if (!m_transfer) m_transferExpiry.stop();
        refreshSnapshot();
        finish(requestId, operation, true);
        return;
    }

    finish(requestId, operation, false, {}, QStringLiteral("unknownOperation"));
}
