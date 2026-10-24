#pragma once

#include "rep_comparisonservice_source.h"
#include "serviceoperation.h"

#include <QPointer>
#include <QHash>
#include <QList>

class ShotComparisonModel;
class ShotHistoryStorage;

class NativeComparisonServiceSource final : public ComparisonStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeComparisonServiceSource(ShotHistoryStorage* storage, QObject* parent = nullptr);
    void query(QString requestId, QVariantList shotIds, qint64 baseShotId, int windowStart) override;
    static QVariantList nestedSamples(const QList<QVariantList>& perShot);

private:
    void finishModel(const QString& requestId, ShotComparisonModel* model);
    void finishError(const QString& requestId, const QString& reason);
    QVariantMap snapshot(const ShotComparisonModel* model) const;

    QPointer<ShotHistoryStorage> m_storage;
    ServiceRequestIds m_requests;
    QHash<QString, QPointer<ShotComparisonModel>> m_models;
    static constexpr int MaximumSelection = 500;
    static constexpr int MaximumInFlight = 16;
    static constexpr qsizetype MaximumSnapshotBytes = 1536 * 1024;
};
