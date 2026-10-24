#pragma once

#include "rep_backupservice_source.h"
#include "serviceoperation.h"
#include "opaquefilebundlestore.h"

#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <atomic>
#include <functional>
#include <memory>

class DatabaseBackupManager;
class SettingsApp;
class NativeImportServiceSource;
class FullBackupRestore;

class NativeBackupServiceSource final : public BackupStateSimpleSource
{
    Q_OBJECT
public:
    NativeBackupServiceSource(DatabaseBackupManager* manager, SettingsApp* app,
                              QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    void openClient(const QString& clientId);
    void closeClient(const QString& clientId);
    void setImportService(NativeImportServiceSource* service);
    void setFullBackupRestore(FullBackupRestore* restore);
    void setRestoreAllowed(std::function<bool()> allowed);

private:
#ifdef DECENZA_TESTING
    friend class tst_BackupOperationPolicy;
#endif
    QVariantMap capture() const;
    void publish(bool changed = false);
    void finish(QString requestId, QString operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishFileOperation(const QString& requestId, const QString& operation,
                             bool success, const QVariantMap& result = {},
                             const QString& reason = {});
    QString backupFilenameForId(const QString& id) const;
    void managerBackupCreated(const QString& path);
    void managerBackupFailed(const QString& error);
    void managerRestoreCompleted(const QString& filename);
    void managerRestoreFailed(const QString& error);
    void prepareBackupDownload(const QString& requestId, const QString& clientId,
                               const QString& backupId);
    void backupDownloadPrepared(const QString& requestId, const QString& clientId,
                                const QString& bundleId, const QString& displayName,
                                const std::shared_ptr<QTemporaryDir>& directory,
                                bool success, const QString& reason);
    void releaseBundleForClient(const QString& clientId);
    void clearBundleLease();

    QPointer<DatabaseBackupManager> m_manager;
    QPointer<SettingsApp> m_app;
    QPointer<NativeImportServiceSource> m_imports;
    QPointer<FullBackupRestore> m_fullRestore;
    std::function<bool()> m_restoreAllowed;
    ServiceRequestIds m_requests;
    QSet<QString> m_clients;
    QString m_activeId;
    QString m_activeOperation;
    QString m_activeClientId;
    QString m_bundleClientId;
    QString m_bundleId;
    QString m_bundleRequestId;
    OpaqueFileBundleStore m_bundles;
    bool m_backupCopyInFlight = false;
    QString m_backupCopyRequestId;
    std::shared_ptr<std::atomic_bool> m_backupCopyCancelled;
    qint64 m_revision = 0;
    QVariantMap m_lastResult;
    QString m_restoreNote;
};
