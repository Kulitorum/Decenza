#include "comparisonservicesource.h"

#include "../models/shotcomparisonmodel.h"
#include "../history/shothistorystorage.h"
#include "servicepayload.h"

#include <QMetaType>
#include <cmath>
#include <limits>

namespace {
bool positiveId(const QVariant& value, qint64* result)
{
    const int type = value.metaType().id();
    if (type == QMetaType::Bool) return false;
    bool ok = false;
    const qint64 id = value.toLongLong(&ok);
    if (!ok || id <= 0) return false;
    if (type == QMetaType::Double || type == QMetaType::Float) {
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number) || std::floor(number) != number
            || number > static_cast<double>(std::numeric_limits<qint64>::max())) return false;
    } else if (type != QMetaType::Int && type != QMetaType::UInt
               && type != QMetaType::LongLong && type != QMetaType::ULongLong) {
        return false;
    }
    *result = id;
    return true;
}

bool boundedVariantMap(const QVariantMap& value, qsizetype maximumBytes)
{
    return ServicePayload::fits(value, maximumBytes, QDataStream::Qt_6_0);
}
}

NativeComparisonServiceSource::NativeComparisonServiceSource(ShotHistoryStorage* storage, QObject* parent)
    : ComparisonStateSimpleSource(parent), m_storage(storage)
{
}

void NativeComparisonServiceSource::query(QString requestId, QVariantList shotIds,
                                           qint64 baseShotId, int windowStart)
{
    const auto started = m_requests.begin(requestId);
    if (started == ServiceRequestIds::Start::DuplicateId) return;
    if (started != ServiceRequestIds::Start::Accepted) {
        emit snapshotReady(requestId, {}, started == ServiceRequestIds::Start::LimitReached
            ? QStringLiteral("busy") : QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_storage) {
        finishError(requestId, QStringLiteral("disconnected"));
        return;
    }
    if (shotIds.size() > MaximumSelection || windowStart < 0 || windowStart > MaximumSelection
        || baseShotId < 0) {
        finishError(requestId, QStringLiteral("invalidRequest"));
        return;
    }
    QList<qint64> seen;
    QVariantList normalized;
    normalized.reserve(shotIds.size());
    for (const QVariant& value : shotIds) {
        qint64 id = 0;
        if (!positiveId(value, &id) || seen.contains(id)) {
            finishError(requestId, QStringLiteral("invalidRequest"));
            return;
        }
        seen.append(id);
        normalized.append(id);
    }
    if (baseShotId > 0 && !normalized.contains(baseShotId)) {
        finishError(requestId, QStringLiteral("invalidRequest"));
        return;
    }
    if (m_models.size() >= MaximumInFlight) {
        finishError(requestId, QStringLiteral("busy"));
        return;
    }

    auto* model = new ShotComparisonModel(this);
    model->setStorage(m_storage);
    m_models.insert(requestId, model);
    connect(model, &ShotComparisonModel::loadingChanged, this, [this, requestId, model] {
        if (!model->loading()) finishModel(requestId, model);
    });
    connect(model, &ShotComparisonModel::shotsChanged, this, [this, requestId, model] {
        if (!model->loading()) finishModel(requestId, model);
    });
    connect(model, &ShotComparisonModel::errorOccurred, this, [this, requestId, model](const QString& reason) {
        if (m_models.value(requestId) == model) finishError(requestId, reason);
    });
    model->setSelection(normalized, baseShotId, windowStart);
    if (!model->loading()) finishModel(requestId, model);
}

void NativeComparisonServiceSource::finishModel(const QString& requestId, ShotComparisonModel* model)
{
    if (!model || m_models.value(requestId) != model || model->loading()) return;
    const QVariantMap result = snapshot(model);
    const bool withinLimit = boundedVariantMap(result, MaximumSnapshotBytes);
    m_models.remove(requestId);
    m_requests.finish(requestId);
    model->deleteLater();
    if (!withinLimit) {
        emit snapshotReady(requestId, {}, QStringLiteral("responseTooLarge"));
        return;
    }
    emit snapshotReady(requestId, result, {});
}

void NativeComparisonServiceSource::finishError(const QString& requestId, const QString& reason)
{
    m_requests.finish(requestId);
    const QPointer<ShotComparisonModel> model = m_models.take(requestId);
    if (model) model->deleteLater();
    emit snapshotReady(requestId, {}, reason);
}

QVariantMap NativeComparisonServiceSource::snapshot(const ShotComparisonModel* model) const
{
    QVariantMap result;
    if (!model) return result;
    result.insert(QStringLiteral("selectedShotIds"), model->selectedShotIds());
    result.insert(QStringLiteral("shotCount"), model->displayShotCount());
    result.insert(QStringLiteral("maxTime"), model->maxTime());
    result.insert(QStringLiteral("baseShotId"), model->baseShotId());
    result.insert(QStringLiteral("comparison"), model->comparison());
    result.insert(QStringLiteral("windowStart"), model->windowStart());
    result.insert(QStringLiteral("totalShots"), model->totalShots());
    result.insert(QStringLiteral("otherWindowSize"), model->otherWindowSize());
    result.insert(QStringLiteral("canShiftLeft"), model->canShiftLeft());
    result.insert(QStringLiteral("canShiftRight"), model->canShiftRight());

    QVariantList infos;
    QList<QVariantList> series[11];
    QList<QVariantList> markers;
    for (int i = 0; i < model->displayShotCount(); ++i) {
        infos.append(model->getShotInfo(i));
        series[0].append(model->getPressureData(i));
        series[1].append(model->getFlowData(i));
        series[2].append(model->getTemperatureData(i));
        series[3].append(model->getWeightData(i));
        series[4].append(model->getWeightFlowRateData(i));
        series[5].append(model->getResistanceData(i));
        series[6].append(model->getConductanceData(i));
        series[7].append(model->getConductanceDerivativeData(i));
        series[8].append(model->getDarcyResistanceData(i));
        series[9].append(model->getTemperatureMixData(i));
        series[10].append(model->getTemperatureMixGoalData(i));
        markers.append(model->getPhaseMarkers(i));
    }
    result.insert(QStringLiteral("shotInfo"), infos);
    static const QStringList names{
        QStringLiteral("pressure"), QStringLiteral("flow"), QStringLiteral("temperature"),
        QStringLiteral("weight"), QStringLiteral("weightFlow"), QStringLiteral("resistance"),
        QStringLiteral("conductance"), QStringLiteral("conductanceDerivative"),
        QStringLiteral("darcyResistance"), QStringLiteral("temperatureMix"),
        QStringLiteral("temperatureMixGoal")
    };
    for (int i = 0; i < names.size(); ++i) result.insert(names.at(i), nestedSamples(series[i]));
    result.insert(QStringLiteral("phaseMarkers"), nestedSamples(markers));
    return result;
}

QVariantList NativeComparisonServiceSource::nestedSamples(const QList<QVariantList>& perShot)
{
    QVariantList result;
    result.reserve(perShot.size());
    for (const QVariantList& samples : perShot)
        result.append(QVariant::fromValue(samples));
    return result;
}
