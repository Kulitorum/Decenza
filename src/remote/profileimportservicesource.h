#pragma once

#include "rep_profileimportservice_source.h"
#include "serviceoperation.h"

#include <QHash>
#include <QPointer>
#include <memory>

class NativeImportServiceSource;
class ProfileImporter;
class QTemporaryDir;

class NativeProfileImportServiceSource final : public ProfileImportStateSimpleSource
{
    Q_OBJECT
public:
    NativeProfileImportServiceSource(ProfileImporter* importer,
                                      NativeImportServiceSource* transfers,
                                      QObject* parent = nullptr);
    ~NativeProfileImportServiceSource() override;

    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
    friend class tst_profileimportservicesource;

    QVariantMap makeSnapshot();
    void refreshSnapshot();
    void projectRows();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishActive(bool success, const QVariantMap& result = {},
                      const QString& reason = {});
    void finishReason(QString requestId, QString operation, const QString& reason);

    QPointer<ProfileImporter> m_importer;
    QPointer<NativeImportServiceSource> m_importService;
    ServiceRequestIds m_requests;
    QHash<QString, QString> m_pathsById;
    QHash<QString, QString> m_idsByPath;
    QString m_activeId;
    QString m_activeOperation;
    QString m_duplicateId;
    std::unique_ptr<QTemporaryDir> m_scanTransfer;
    bool m_importAfterScan = false;
    QVariantList m_projectedRows;
    qsizetype m_totalAvailableProfiles = 0;
    bool m_profilesTruncated = false;
};
