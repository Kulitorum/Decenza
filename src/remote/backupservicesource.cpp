#include "backupservicesource.h"
#include "backupoperationpolicy.h"
#include "servicevalue.h"

#include "core/databasebackupmanager.h"
#include "core/settings_app.h"
#include "core/fullbackuprestore.h"
#include "importservicesource.h"
#include "filebundlepolicy.h"
#include "serviceoperation.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMetaObject>
#include <QMetaType>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>
#include <QTemporaryDir>
#include <utility>

namespace {
QString startReason(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("tooManyRequests");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}

QString safeError(QString error)
{
    if (error.contains(QLatin1Char('/')) || error.contains(QLatin1Char('\\')))
        return QStringLiteral("The backup operation failed. Check available storage and permissions.");
    if (error.size() > 512) error.truncate(512);
    return error;
}
}

NativeBackupServiceSource::NativeBackupServiceSource(DatabaseBackupManager* manager,
                                                     SettingsApp* app, QObject* parent)
    : BackupStateSimpleSource(parent), m_manager(manager), m_app(app)
{
    if (m_manager) {
        connect(m_manager, &DatabaseBackupManager::availableBackupsChanged,
                this, [this] { publish(true); });
        connect(m_manager, &DatabaseBackupManager::aiConversationNoteChanged,
                this, [this] { m_restoreNote = m_manager->aiConversationNote(); publish(true); });
        connect(m_manager, &DatabaseBackupManager::backupCreated,
                this, &NativeBackupServiceSource::managerBackupCreated);
        connect(m_manager, &DatabaseBackupManager::backupFailed,
                this, &NativeBackupServiceSource::managerBackupFailed);
        connect(m_manager, &DatabaseBackupManager::restoreCompleted,
                this, &NativeBackupServiceSource::managerRestoreCompleted);
        connect(m_manager, &DatabaseBackupManager::restoreFailed,
                this, &NativeBackupServiceSource::managerRestoreFailed);
        connect(m_manager, &QObject::destroyed, this, [this] {
            m_manager = nullptr;
            publish(true);
        });
    }
    if (m_app) {
        connect(m_app, &SettingsApp::dailyBackupHourChanged, this, [this] { publish(true); });
        connect(m_app, &QObject::destroyed, this, [this] { m_app = nullptr; publish(true); });
    }
    publish();
}

void NativeBackupServiceSource::setImportService(NativeImportServiceSource* service)
{
    m_imports = service;
}

void NativeBackupServiceSource::setRestoreAllowed(std::function<bool()> allowed)
{
    m_restoreAllowed = std::move(allowed);
}

void NativeBackupServiceSource::setFullBackupRestore(FullBackupRestore* restore)
{
    if (m_fullRestore) disconnect(m_fullRestore, nullptr, this, nullptr);
    m_fullRestore = restore;
    if (!restore) return;
    connect(restore, &FullBackupRestore::finished, this,
        [this](const QString& requestId, bool success, const QVariantMap& result,
               const QString& reason, int) {
        if (requestId == m_activeId && m_activeOperation == QLatin1String("restoreUploadedBackup")) {
            m_restoreNote = result.value(QStringLiteral("aiConversationsNote")).toString();
            finish(requestId, m_activeOperation, success, result, reason);
        }
    });
    connect(restore, &FullBackupRestore::busyChanged, this, [this] { publish(true); });
}

QVariantMap NativeBackupServiceSource::capture() const
{
    QVariantList backups;
    if (m_manager) {
        static const QRegularExpression rowPattern(QStringLiteral("^(.+)\\|(shots_backup_[0-9]{8}\\.(?:zip|db))$"));
        for (const QString& row : m_manager->availableBackups()) {
            const auto match = rowPattern.match(row);
            if (!match.hasMatch() || !BackupOperationPolicy::safeBackupFilename(match.captured(2))) continue;
            backups.append(QVariantMap{
                {QStringLiteral("id"), BackupOperationPolicy::backupIdForFilename(match.captured(2))},
                {QStringLiteral("displayName"), match.captured(1).left(128)},
                {QStringLiteral("downloadable"),
                 BackupOperationPolicy::downloadableBackupFilename(match.captured(2))}
            });
        }
    }
    return {
        {QStringLiteral("backups"), backups},
        {QStringLiteral("hasStoragePermission"), m_manager && m_manager->hasStoragePermission()},
        {QStringLiteral("dailyBackupHour"), m_app ? m_app->dailyBackupHour() : -1},
        {QStringLiteral("revision"), m_revision},
        {QStringLiteral("backupBusy"), m_activeOperation == QLatin1String("createBackup")},
        {QStringLiteral("restoreBusy"), m_activeOperation == QLatin1String("restoreBackup")
            || m_activeOperation == QLatin1String("restoreUploadedBackup")
            || (m_fullRestore && m_fullRestore->busy())},
        {QStringLiteral("activeOperation"), m_activeOperation},
        {QStringLiteral("aiConversationNote"), m_restoreNote},
        {QStringLiteral("lastResult"), m_lastResult}
    };
}

void NativeBackupServiceSource::publish(bool changed)
{
    if (changed && m_revision < 9007199254740991LL) ++m_revision;
    const QVariantMap state = capture();
    setSnapshot(state);
    setReady(m_manager && m_app);
}

void NativeBackupServiceSource::refresh()
{
    // `DatabaseBackupManager::start()` owns the existing one-time catalog load,
    // and successful backups refresh it. Do not rescan the host directory from
    // QML visibility changes or service construction.
    publish();
}

QString NativeBackupServiceSource::backupFilenameForId(const QString& id) const
{
    if (!m_manager || !BackupOperationPolicy::validBackupId(id)) return {};
    static const QRegularExpression rowPattern(QStringLiteral("^(.+)\\|(shots_backup_[0-9]{8}\\.(?:zip|db))$"));
    for (const QString& row : m_manager->availableBackups()) {
        const auto match = rowPattern.match(row);
        if (match.hasMatch() && BackupOperationPolicy::safeBackupFilename(match.captured(2))
            && BackupOperationPolicy::backupIdForFilename(match.captured(2)) == id)
            return match.captured(2);
    }
    return {};
}

void NativeBackupServiceSource::finish(QString id, QString operation, bool success,
                                       const QVariantMap& result, const QString& reason)
{
    if (!m_requests.finish(id)) return;
    if (m_activeId == id) {
        m_activeId.clear();
        m_activeOperation.clear();
        m_activeClientId.clear();
    }
    m_lastResult = {{QStringLiteral("operation"), operation},
                    {QStringLiteral("success"), success},
                    {QStringLiteral("reason"), reason},
                    {QStringLiteral("result"), result}};
    const QPointer<NativeBackupServiceSource> guard(this);
    publish(true);
    if (guard) emit operationCompleted(id, operation, success, result, reason);
}

void NativeBackupServiceSource::finishFileOperation(const QString& id, const QString& operation,
                                                    bool success, const QVariantMap& result,
                                                    const QString& reason)
{
    if (!m_requests.finish(id)) return;
    emit operationCompleted(id, operation, success, result, reason);
}

void NativeBackupServiceSource::openClient(const QString& clientId)
{
    if (!clientId.isEmpty() && clientId.size() <= 128)
        m_clients.insert(clientId);
}

void NativeBackupServiceSource::releaseBundleForClient(const QString& clientId)
{
    if (m_bundleClientId != clientId) return;
    clearBundleLease();
}

void NativeBackupServiceSource::clearBundleLease()
{
    if (m_bundles.hasBundle()) {
        QString ignored;
        m_bundles.release({{QStringLiteral("bundleId"), m_bundleId}}, &ignored);
    }
    m_bundleClientId.clear();
    m_bundleId.clear();
    m_bundleRequestId.clear();
}

void NativeBackupServiceSource::closeClient(const QString& clientId)
{
    if (!m_clients.remove(clientId)) return;
    const bool cancelPreparation = m_activeClientId == clientId
        && m_activeOperation == QLatin1String("prepareBackupDownload");
    QString retiredId;
    if (cancelPreparation) {
        retiredId = std::exchange(m_activeId, {});
        m_activeOperation.clear();
        m_activeClientId.clear();
        if (m_backupCopyInFlight && m_backupCopyRequestId == retiredId
            && m_backupCopyCancelled)
            m_backupCopyCancelled->store(true, std::memory_order_relaxed);
    }
    if (retiredId.isEmpty() && m_bundleClientId == clientId)
        retiredId = m_bundleRequestId;
    releaseBundleForClient(clientId);
    if (!retiredId.isEmpty()) {
        const QPointer<NativeBackupServiceSource> guard(this);
        if (cancelPreparation) publish(true);
        if (!guard) return;
        finishFileOperation(retiredId, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("cancelled"));
    }
}

void NativeBackupServiceSource::prepareBackupDownload(const QString& requestId,
                                                      const QString& clientId,
                                                      const QString& backupId)
{
    if (!m_manager || !m_clients.contains(clientId)) {
        finishFileOperation(requestId, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("clientUnavailable"));
        return;
    }
    if (!m_activeId.isEmpty() || m_backupCopyInFlight || m_bundles.hasBundle()) {
        finishFileOperation(requestId, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("busy"));
        return;
    }
    const QString filename = backupFilenameForId(backupId);
    if (!BackupOperationPolicy::downloadableBackupFilename(filename)) {
        finishFileOperation(requestId, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("unknownBackup"));
        return;
    }
    const QString sourcePath = m_manager->backupPathForFilename(filename);
    if (sourcePath.isEmpty()) {
        finishFileOperation(requestId, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("unknownBackup"));
        return;
    }
    auto directory = std::make_shared<QTemporaryDir>();
    if (!directory->isValid()) {
        finishFileOperation(requestId, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("storageUnavailable"));
        return;
    }
    const QString bundleId = ServiceOperation::newId().toLower();
    const QString displayName = QFileInfo(sourcePath).fileName();
    const QString destinationPath = directory->filePath(bundleId + QStringLiteral(".zip"));
    m_activeId = requestId;
    m_activeOperation = QStringLiteral("prepareBackupDownload");
    m_activeClientId = clientId;
    const QPointer<NativeBackupServiceSource> publishGuard(this);
    publish(true);
    if (!publishGuard) return;
    if (m_activeId != requestId || m_activeOperation != QLatin1String("prepareBackupDownload")
        || m_activeClientId != clientId || !m_clients.contains(clientId))
        return;

    QPointer<NativeBackupServiceSource> self(this);
    QObject* completionContext = QCoreApplication::instance();
    if (!completionContext) {
        const QString id = std::exchange(m_activeId, {});
        m_activeOperation.clear();
        m_activeClientId.clear();
        const QPointer<NativeBackupServiceSource> guard(this);
        publish(true);
        if (!guard) return;
        finishFileOperation(id, QStringLiteral("prepareBackupDownload"), false, {},
                            QStringLiteral("unavailable"));
        return;
    }
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_backupCopyInFlight = true;
    m_backupCopyRequestId = requestId;
    m_backupCopyCancelled = cancelled;
    QThreadPool::globalInstance()->start(QRunnable::create(
        [self, completionContext, requestId, clientId, sourcePath, destinationPath,
         bundleId, displayName, directory, cancelled] {
            QString failure;
            const QFileInfo before(sourcePath);
            if (!before.isFile() || before.isSymLink() || before.size() <= 0) {
                failure = QStringLiteral("invalidBackupFile");
            } else if (before.size() > FileBundlePolicy::MaximumBackupBytes) {
                failure = QStringLiteral("fileTooLarge");
            } else {
                QFile source(sourcePath);
                QFile destination(destinationPath);
                if (!source.open(QIODevice::ReadOnly)
                    || !destination.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                    failure = QStringLiteral("backupReadFailed");
                } else {
                    qint64 copied = 0;
                    while (!source.atEnd()) {
                        if (cancelled->load(std::memory_order_relaxed)) {
                            failure = QStringLiteral("cancelled");
                            break;
                        }
                        const QByteArray chunk = source.read(FileBundlePolicy::MaximumChunkBytes);
                        if (chunk.isEmpty()) {
                            if (source.error() != QFileDevice::NoError)
                                failure = QStringLiteral("backupReadFailed");
                            break;
                        }
                        copied += chunk.size();
                        if (copied > FileBundlePolicy::MaximumBackupBytes
                            || destination.write(chunk) != chunk.size()) {
                            failure = QStringLiteral("backupCopyFailed");
                            break;
                        }
                    }
                    if (failure.isEmpty() && cancelled->load(std::memory_order_relaxed))
                        failure = QStringLiteral("cancelled");
                    if (failure.isEmpty() && (!destination.flush()
                        || copied != before.size() || source.size() != before.size()))
                        failure = QStringLiteral("backupChanged");
                    destination.close();
                    source.close();
                    const QFileInfo after(sourcePath);
                    if (failure.isEmpty() && (after.isSymLink() || !after.isFile()
                        || after.size() != before.size()
                        || after.lastModified() != before.lastModified()))
                        failure = QStringLiteral("backupChanged");
                }
            }
            if (!failure.isEmpty()) QFile::remove(destinationPath);
            QMetaObject::invokeMethod(completionContext,
                [self, requestId, clientId, bundleId, displayName, directory, failure] {
                if (self)
                    self->backupDownloadPrepared(requestId, clientId, bundleId, displayName, directory,
                                                  failure.isEmpty(), failure);
            }, Qt::QueuedConnection);
        }));
}

void NativeBackupServiceSource::backupDownloadPrepared(
    const QString& requestId, const QString& clientId, const QString& bundleId,
    const QString& displayName, const std::shared_ptr<QTemporaryDir>& directory,
    bool success, const QString& reason)
{
    const QString operation = QStringLiteral("prepareBackupDownload");
    if (m_backupCopyInFlight && m_backupCopyRequestId == requestId) {
        m_backupCopyInFlight = false;
        m_backupCopyRequestId.clear();
        m_backupCopyCancelled.reset();
    }
    if (m_activeId != requestId || m_activeOperation != operation
        || m_activeClientId != clientId || !m_clients.contains(clientId))
        return;

    QVariantMap result;
    QString finalReason = reason;
    if (success) {
        result = m_bundles.adopt(directory, bundleId, bundleId + QStringLiteral(".zip"),
                                 displayName, FileBundlePolicy::MaximumBackupBytes,
                                 QStringLiteral("application/zip"),
                                 QStringLiteral("ZIP archives (*.zip)"));
        success = !result.isEmpty();
        if (success) {
            m_bundleClientId = clientId;
            m_bundleId = bundleId;
            m_bundleRequestId = requestId;
        } else {
            result.clear();
            finalReason = QStringLiteral("bundleInvalid");
        }
    }
    const QString id = std::exchange(m_activeId, {});
    m_activeOperation.clear();
    m_activeClientId.clear();
    const QPointer<NativeBackupServiceSource> guard(this);
    publish(true);
    if (!guard) return;
    if (success && (!m_bundles.hasBundle() || m_bundleClientId != clientId
                    || m_bundleId != bundleId || m_bundleRequestId != requestId)) {
        success = false;
        result.clear();
        finalReason = QStringLiteral("cancelled");
    }
    finishFileOperation(id, operation, success, result, finalReason);
}

void NativeBackupServiceSource::managerBackupCreated(const QString&)
{
    if (m_activeOperation == QLatin1String("createBackup")) {
        finish(m_activeId, m_activeOperation, true, {{QStringLiteral("created"), true}});
    } else {
        publish(true);
    }
}

void NativeBackupServiceSource::managerBackupFailed(const QString& error)
{
    // The canonical manager also emits this signal when another caller is
    // rejected because our request already owns the active backup slot.
    if (m_manager && m_manager->isBackupInProgress()) {
        publish();
    } else if (m_activeOperation == QLatin1String("createBackup"))
        finish(m_activeId, m_activeOperation, false, {{QStringLiteral("error"), safeError(error)}},
               QStringLiteral("operationFailed"));
    else
        publish(true);
}

void NativeBackupServiceSource::managerRestoreCompleted(const QString& filename)
{
    if (m_activeOperation == QLatin1String("restoreBackup")
        || m_activeOperation == QLatin1String("restoreUploadedBackup")) {
        finish(m_activeId, m_activeOperation, true,
               {{QStringLiteral("backupId"), BackupOperationPolicy::backupIdForFilename(QFileInfo(filename).fileName())}});
    } else {
        publish(true);
    }
}

void NativeBackupServiceSource::managerRestoreFailed(const QString& error)
{
    if (m_manager && m_manager->isRestoreInProgress()) {
        publish();
    } else if (m_activeOperation == QLatin1String("restoreBackup")
               || m_activeOperation == QLatin1String("restoreUploadedBackup"))
        finish(m_activeId, m_activeOperation, false, {{QStringLiteral("error"), safeError(error)}},
               QStringLiteral("operationFailed"));
    else
        publish(true);
}

void NativeBackupServiceSource::execute(QString id, QString operation, QVariantMap args)
{
    const QString clientId = args.take(QStringLiteral("clientId")).toString();
    const auto start = m_requests.begin(id);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(id, operation, false, {}, startReason(start));
        return;
    }
    if (!BackupOperationPolicy::supported(operation)) {
        finish(id, operation, false, {}, QStringLiteral("unsupportedOperation")); return;
    }
    if (!BackupOperationPolicy::validArguments(operation, args)) {
        if (BackupOperationPolicy::isFileOperation(operation)) {
            finishFileOperation(id, operation, false, {}, QStringLiteral("invalidArguments"));
            return;
        }
        finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return;
    }
    if (BackupOperationPolicy::isFileOperation(operation)) {
        if (!m_bundles.hasBundle()) {
            clearBundleLease();
        }
        if (clientId.isEmpty() || !m_clients.contains(clientId)) {
            finishFileOperation(id, operation, false, {}, QStringLiteral("clientUnavailable"));
            return;
        }
        if (operation == QLatin1String("prepareBackupDownload")) {
            prepareBackupDownload(id, clientId,
                args.value(QStringLiteral("backupId")).toString());
            return;
        }
        if (operation == QLatin1String("cancelFileOperation")) {
            const QString targetId = args.value(QStringLiteral("requestId")).toString();
            const bool activeMatch = targetId == m_activeId
                && m_activeOperation == QLatin1String("prepareBackupDownload")
                && m_activeClientId == clientId;
            const bool bundleMatch = targetId == m_bundleRequestId
                && m_bundleClientId == clientId && m_bundles.hasBundle();
            const bool cancelled = activeMatch || bundleMatch;
            if (cancelled) {
                if (activeMatch) {
                    if (m_backupCopyInFlight && m_backupCopyRequestId == targetId
                        && m_backupCopyCancelled)
                        m_backupCopyCancelled->store(true, std::memory_order_relaxed);
                    m_activeId.clear();
                    m_activeOperation.clear();
                    m_activeClientId.clear();
                }
                if (bundleMatch) clearBundleLease();
                const QPointer<NativeBackupServiceSource> guard(this);
                if (activeMatch) publish(true);
                if (!guard) return;
                finishFileOperation(targetId, QStringLiteral("prepareBackupDownload"), false, {},
                                    QStringLiteral("cancelled"));
                if (!guard) return;
            }
            finishFileOperation(id, operation, true,
                                {{QStringLiteral("cancelled"), cancelled}});
            return;
        }
        if (m_bundleClientId != clientId || !m_bundles.hasBundle()
            || args.value(QStringLiteral("bundleId")).toString() != m_bundleId) {
            finishFileOperation(id, operation, false, {}, QStringLiteral("invalidBundle"));
            return;
        }
        QString reason;
        if (operation == QLatin1String("readBundleChunk")) {
            const QVariantMap chunk = m_bundles.readChunk(args, &reason);
            finishFileOperation(id, operation, reason.isEmpty(), chunk, reason);
            return;
        }
        const bool released = m_bundles.release(args, &reason);
        if (released) {
            clearBundleLease();
        }
        finishFileOperation(id, operation, released, {}, reason);
        return;
    }
    if (!m_manager || !m_app) {
        finish(id, operation, false, {}, QStringLiteral("unavailable")); return;
    }
    if (operation == QLatin1String("setBackupHour")) {
        int hour = -1, expectedHour = -2;
        if (!ServiceValue::boundedInteger(args.value(QStringLiteral("hour")), -1, 23, &hour)
            || !ServiceValue::boundedInteger(args.value(QStringLiteral("expectedHour")), -1, 23, &expectedHour)) {
            finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return;
        }
        if (m_app->dailyBackupHour() != expectedHour) {
            finish(id, operation, false, {{QStringLiteral("dailyBackupHour"), m_app->dailyBackupHour()}},
                   QStringLiteral("conflict")); return;
        }
        m_app->setDailyBackupHour(hour);
        finish(id, operation, true, {{QStringLiteral("dailyBackupHour"), hour}});
        return;
    }
    if (operation == QLatin1String("requestStoragePermission")) {
        m_manager->requestStoragePermission();
        publish(true);
        finish(id, operation, true, {{QStringLiteral("hasStoragePermission"), m_manager->hasStoragePermission()}});
        return;
    }
    if (!m_activeId.isEmpty() || (m_fullRestore && m_fullRestore->busy())) {
        finish(id, operation, false, {}, QStringLiteral("busy")); return;
    }
    if ((operation == QLatin1String("restoreBackup")
         || operation == QLatin1String("restoreUploadedBackup"))
        && (!m_restoreAllowed || !m_restoreAllowed())) {
        finish(id, operation, false, {}, QStringLiteral("operationNotAllowed")); return;
    }
    if (operation == QLatin1String("createBackup")) {
        m_activeId = id;
        m_activeOperation = operation;
        publish(true);
        const bool started = m_manager->createBackup(true);
        if (!started && m_activeId == id)
            finish(id, operation, false, {}, QStringLiteral("busy"));
        return;
    }
    if (operation == QLatin1String("restoreUploadedBackup")) {
        if (!m_imports || !m_fullRestore) {
            finish(id, operation, false, {}, QStringLiteral("unavailable")); return;
        }
        const QPointer<NativeBackupServiceSource> guard(this);
        auto upload = m_imports->takeCompletedTransfer(
            args.value(QStringLiteral("transferId")).toString(), QStringLiteral("backupArchive"));
        if (!guard) return;
        if (!upload) {
            finish(id, operation, false, {}, QStringLiteral("unknownTransfer")); return;
        }
        std::shared_ptr<QTemporaryDir> inputLease(std::move(upload));
        const QFileInfoList files = QDir(inputLease->path()).entryInfoList(QDir::Files);
        if (files.size() != 1) {
            finish(id, operation, false, {}, QStringLiteral("invalidBackupFile")); return;
        }
        const QFileInfo file = files.first();
        const bool merge = args.value(QStringLiteral("merge")).toBool();
        const bool shots = args.value(QStringLiteral("shots")).toBool();
        const bool settings = args.value(QStringLiteral("settings")).toBool();
        const bool profiles = args.value(QStringLiteral("profiles")).toBool();
        const bool media = args.value(QStringLiteral("media")).toBool();
        m_activeId = id;
        m_activeOperation = operation;
        m_restoreNote.clear();
        publish(true);
        if (!guard || m_activeId != id || m_activeOperation != operation) return;
        if (file.suffix().compare(QLatin1String("dcbackup"), Qt::CaseInsensitive) == 0) {
            const bool started = m_fullRestore->restoreFile(id, file.absoluteFilePath(),
                FullBackupRestore::Options{merge, shots, settings, profiles, media}, inputLease);
            if (guard && !started && m_activeId == id)
                finish(id, operation, false, {}, QStringLiteral("operationNotAllowed"));
        } else {
            const bool started = m_manager->restoreBackupFile(file.absoluteFilePath(), merge,
                shots, settings, profiles, media, inputLease);
            if (guard && !started && m_activeId == id)
                finish(id, operation, false, {}, QStringLiteral("busy"));
        }
        return;
    }
    if (operation == QLatin1String("restoreBackup")) {
        const QString filename = backupFilenameForId(args.value(QStringLiteral("backupId")).toString());
        if (filename.isEmpty()) { finish(id, operation, false, {}, QStringLiteral("unknownBackup")); return; }
        m_activeId = id;
        m_activeOperation = operation;
        m_restoreNote.clear();
        publish(true);
        const bool started = m_manager->restoreBackup(
            filename, args.value(QStringLiteral("merge")).toBool(),
            args.value(QStringLiteral("shots")).toBool(),
            args.value(QStringLiteral("settings")).toBool(),
            args.value(QStringLiteral("profiles")).toBool(),
            args.value(QStringLiteral("media")).toBool());
        if (!started && m_activeId == id)
            finish(id, operation, false, {}, QStringLiteral("busy"));
    }
}
