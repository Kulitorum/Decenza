#pragma once

#include "rep_visualizerprofileimportservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class VisualizerImporter;

class NativeVisualizerProfileImportServiceSource final : public VisualizerProfileImportStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeVisualizerProfileImportServiceSource(VisualizerImporter* importer,
                                                          QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;

private:
    friend class tst_visualizerprofileimportservicesource;
    void refreshSnapshot();
    void projectSharedShots();
    void completeFetchIfStopped();
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void finishImport(bool success, const QString& titleOrError);
    void reject(const QString& requestId, const QString& operation,
                const QString& reason, bool tracked);
    void rejectResolution(const QString& requestId, const QString& operation,
                          const QString& reason);
    QString startReason(ServiceRequestIds::Start start) const;

    QPointer<VisualizerImporter> m_importer;
    ServiceRequestIds m_requests;
    QString m_activeImportId;
    QString m_activeImportOperation;
    QString m_fetchRequestId;
    QString m_fetchOperation;
    QString m_duplicateRequestId;
    bool m_fetchDataArrived = false;
    bool m_sharedShotsTruncated = false;
    QVariantList m_projectedSharedShots;
};
