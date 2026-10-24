#pragma once

#include "rep_importservice_source.h"
#include "serviceoperation.h"

#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <memory>

class ProfileImporter;
class ShotImporter;
class ShotHistoryStorage;

class NativeImportServiceSource final : public ImportStateSimpleSource
{
    Q_OBJECT
public:
    NativeImportServiceSource(ShotImporter* shots, ProfileImporter* profiles,
                              ShotHistoryStorage* storage = nullptr,
                              QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
    // Host-only handoff to the language/profile consumers. It never crosses
    // QtRO or QML and returns an upload only after every chunk landed.
    std::unique_ptr<QTemporaryDir> takeCompletedTransfer(const QString& transferId,
                                                          const QString& expectedKind);
private:
#ifdef DECENZA_TESTING
    friend class tst_profileimportservicesource;
    friend class LanguageServiceSourceTest;
#endif
    struct TransferFile { QString name; QString path; qint64 expectedSize = 0; qint64 offset = 0; };
    struct Transfer {
        QString id;
        QString kind;
        bool overwrite = false;
        bool merge = false;
        bool completed = false;
        qint64 totalBytes = 0;
        std::unique_ptr<QTemporaryDir> directory;
        QList<TransferFile> files;
    };
    QVariantMap makeSnapshot() const;
    void refreshSnapshot();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishActive(bool success, const QVariantMap& result = {}, const QString& reason = {});
    void maybeFinishDeviceImport();
    QString reasonFor(ServiceRequestIds::Start start) const;

    QPointer<ShotImporter> m_shots;
    QPointer<ProfileImporter> m_profiles;
    QPointer<ShotHistoryStorage> m_storage;
    ServiceRequestIds m_requests;
    QTimer m_transferExpiry;
    std::unique_ptr<Transfer> m_transfer;
    QString m_activeRequestId;
    QString m_activeOperation;
    bool m_waitingForShotImport = false;
    bool m_waitingForProfileImport = false;
    bool m_waitingForDatabaseImport = false;
    QVariantMap m_deviceImportResult;
    QString m_deviceImportError;
    bool m_shotDetected = false;
    bool m_profileDetected = false;
    std::unique_ptr<Transfer> m_activeTransfer;
};
