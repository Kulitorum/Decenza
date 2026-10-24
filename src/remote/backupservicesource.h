#pragma once

#include "rep_backupservice_source.h"
#include "serviceoperation.h"

#include <QPointer>

class DatabaseBackupManager;
class SettingsApp;

class NativeBackupServiceSource final : public BackupStateSimpleSource
{
    Q_OBJECT
public:
    NativeBackupServiceSource(DatabaseBackupManager* manager, SettingsApp* app,
                              QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
#ifdef DECENZA_TESTING
    friend class tst_BackupOperationPolicy;
#endif
    QVariantMap capture() const;
    void publish(bool changed = false);
    void finish(QString requestId, QString operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    QString backupFilenameForId(const QString& id) const;
    void managerBackupCreated(const QString& path);
    void managerBackupFailed(const QString& error);
    void managerRestoreCompleted(const QString& filename);
    void managerRestoreFailed(const QString& error);

    QPointer<DatabaseBackupManager> m_manager;
    QPointer<SettingsApp> m_app;
    ServiceRequestIds m_requests;
    QString m_activeId;
    QString m_activeOperation;
    qint64 m_revision = 0;
    QVariantMap m_lastResult;
};
