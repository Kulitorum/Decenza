#pragma once

#include "rep_datamigrationservice_source.h"
#include "serviceoperation.h"
#include <QPointer>
#include <QString>

class DataMigrationClient;

class NativeDataMigrationServiceSource final : public DataMigrationStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeDataMigrationServiceSource(DataMigrationClient* client,
                                             QObject* parent = nullptr);
    void refresh() override;
    void execute(QString requestId, QString operation, QVariantMap arguments) override;
private:
    QVariantMap makeSnapshot() const;
    void refreshSnapshot();
    void completeActive(bool success, const QVariantMap& result = {}, const QString& reason = {});
    void complete(const QString& requestId, const QString& operation, bool success,
                  const QVariantMap& result = {}, const QString& reason = {});
    void failActive(const QString& reason);
    QPointer<DataMigrationClient> m_client;
    QString m_activeRequestId;
    QString m_activeOperation;
    ServiceRequestIds m_requests;
    bool m_ready = false;
};
