#include "backupservicesource.h"
#include "backupoperationpolicy.h"
#include "servicevalue.h"

#include "core/databasebackupmanager.h"
#include "core/settings_app.h"

#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>

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
                this, [this] { publish(true); });
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
                {QStringLiteral("displayName"), match.captured(1).left(128)}
            });
        }
    }
    return {
        {QStringLiteral("backups"), backups},
        {QStringLiteral("hasStoragePermission"), m_manager && m_manager->hasStoragePermission()},
        {QStringLiteral("dailyBackupHour"), m_app ? m_app->dailyBackupHour() : -1},
        {QStringLiteral("revision"), m_revision},
        {QStringLiteral("backupBusy"), m_activeOperation == QLatin1String("createBackup")},
        {QStringLiteral("restoreBusy"), m_activeOperation == QLatin1String("restoreBackup")},
        {QStringLiteral("activeOperation"), m_activeOperation},
        {QStringLiteral("aiConversationNote"), m_manager ? m_manager->aiConversationNote() : QString()},
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
    }
    m_lastResult = {{QStringLiteral("operation"), operation},
                    {QStringLiteral("success"), success},
                    {QStringLiteral("reason"), reason},
                    {QStringLiteral("result"), result}};
    publish(true);
    emit operationCompleted(id, operation, success, result, reason);
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
    if (m_activeOperation == QLatin1String("restoreBackup")) {
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
    } else if (m_activeOperation == QLatin1String("restoreBackup"))
        finish(m_activeId, m_activeOperation, false, {{QStringLiteral("error"), safeError(error)}},
               QStringLiteral("operationFailed"));
    else
        publish(true);
}

void NativeBackupServiceSource::execute(QString id, QString operation, QVariantMap args)
{
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
        finish(id, operation, false, {}, QStringLiteral("invalidArguments")); return;
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
    if (!m_activeId.isEmpty()) {
        finish(id, operation, false, {}, QStringLiteral("busy")); return;
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
    if (operation == QLatin1String("restoreBackup")) {
        const QString filename = backupFilenameForId(args.value(QStringLiteral("backupId")).toString());
        if (filename.isEmpty()) { finish(id, operation, false, {}, QStringLiteral("unknownBackup")); return; }
        m_activeId = id;
        m_activeOperation = operation;
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
