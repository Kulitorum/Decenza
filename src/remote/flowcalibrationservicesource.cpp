#include "flowcalibrationservicesource.h"

#include "../core/settings_calibration.h"
#include "../models/flowcalibrationmodel.h"
#include "../models/flowcalibrationmath.h"

#include <cmath>
#include <limits>

namespace { constexpr qint64 kMaxSafeRevision = 9007199254740991LL; }

NativeFlowCalibrationSource::NativeFlowCalibrationSource(FlowCalibrationModel* model,
                                                           SettingsCalibration* settings,
                                                           QObject* parent)
    : FlowCalibrationStateSimpleSource(parent), m_model(model), m_settings(settings)
{
    if (m_model) {
        connect(m_model, &FlowCalibrationModel::loadingChanged, this, [this] {
            setLoading(m_model && m_model->loading());
            if (m_model && !m_model->loading() && !m_loadingRequestId.isEmpty())
                completeLoad();
        });
        connect(m_model, &FlowCalibrationModel::navigationChanged, this, [this] {
            setShotListRevision(m_model ? m_model->shotListRevision() : 0);
        });
        connect(m_model, &QObject::destroyed, this, [this] {
            m_model = nullptr;
            setFailure(m_loadingRequestId, m_loadingOperation, QStringLiteral("unavailable"));
            setReady(false);
        });
    }
    if (m_settings) {
        connect(m_settings, &SettingsCalibration::flowCalibrationMultiplierChanged, this, [this] {
            if (m_calibrationRevision < kMaxSafeRevision) ++m_calibrationRevision;
            publish();
        });
        connect(m_settings, &QObject::destroyed, this, [this] { m_settings = nullptr; setReady(false); });
    }
    publish();
}

void NativeFlowCalibrationSource::refresh() { publish(); }

void NativeFlowCalibrationSource::publish()
{
    if (!m_settings || !m_model) {
        setReady(false);
        setErrorMessage(QStringLiteral("Flow calibration is unavailable"));
        return;
    }
    setSavedMultiplier(m_settings->flowCalibrationMultiplier());
    setCalibrationRevision(m_calibrationRevision);
    setShotListRevision(m_model->shotListRevision());
    setLoading(m_model->loading());
    setErrorMessage(m_model->errorMessage());
    setReady(true);
}

void NativeFlowCalibrationSource::loadRecentShots(QString requestId)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, QStringLiteral("loadRecentShots"), false, {},
            start == ServiceRequestIds::Start::LimitReached ? QStringLiteral("tooManyRequests")
                                                            : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_model || !m_settings) { finish(requestId, QStringLiteral("loadRecentShots"), false, {}, QStringLiteral("unavailable")); return; }
    if (m_model->loading() || !m_loadingRequestId.isEmpty()) { finish(requestId, QStringLiteral("loadRecentShots"), false, {}, QStringLiteral("busy")); return; }
    beginLoad(requestId, QStringLiteral("loadRecentShots"), -1);
    m_model->loadRecentShots();
    if (!m_model->loading()) completeLoad();
}

void NativeFlowCalibrationSource::loadShot(QString requestId, int index,
                                            quint64 expectedShotListRevision)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, QStringLiteral("loadShot"), false, {},
            start == ServiceRequestIds::Start::LimitReached ? QStringLiteral("tooManyRequests")
                                                            : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_model || !m_settings) { finish(requestId, QStringLiteral("loadShot"), false, {}, QStringLiteral("unavailable")); return; }
    if (m_model->loading() || !m_loadingRequestId.isEmpty()) { finish(requestId, QStringLiteral("loadShot"), false, {}, QStringLiteral("busy")); return; }
    if (expectedShotListRevision != m_model->shotListRevision()) { finish(requestId, QStringLiteral("loadShot"), false, {}, QStringLiteral("conflict")); return; }
    if (index < 0 || index >= m_model->shotCount()) { finish(requestId, QStringLiteral("loadShot"), false, {}, QStringLiteral("invalidIndex")); return; }
    if (index == m_model->currentShotIndex()) {
        finish(requestId, QStringLiteral("loadShot"), true, currentShot());
        return;
    }
    beginLoad(requestId, QStringLiteral("loadShot"), index);
    m_model->selectShot(index);
    if (!m_model->loading()) completeLoad();
}

void NativeFlowCalibrationSource::beginLoad(const QString& requestId, const QString& operation, int index)
{
    m_loadingRequestId = requestId;
    m_loadingOperation = operation;
    m_requestedIndex = index;
    setLoading(true);
}

QVariantMap NativeFlowCalibrationSource::currentShot() const
{
    if (!m_model) return {};
    return {
        {QStringLiteral("originalFlowData"), m_model->originalFlowData()},
        {QStringLiteral("weightFlowData"), m_model->weightFlowData()},
        {QStringLiteral("pressureData"), m_model->pressureData()},
        {QStringLiteral("maxTime"), m_model->maxTime()},
        {QStringLiteral("shotInfo"), m_model->shotInfo()},
        {QStringLiteral("hasData"), m_model->hasData()},
        {QStringLiteral("errorMessage"), m_model->errorMessage()},
        {QStringLiteral("currentShotIndex"), m_model->currentShotIndex()},
        {QStringLiteral("shotCount"), m_model->shotCount()},
        {QStringLiteral("hasPreviousShot"), m_model->hasPreviousShot()},
        {QStringLiteral("hasNextShot"), m_model->hasNextShot()},
        {QStringLiteral("shotRecordedMultiplier"), m_model->shotRecordedMultiplier()},
        {QStringLiteral("shotMultiplierRecorded"), m_model->shotMultiplierRecorded()},
        {QStringLiteral("shotListRevision"), QVariant::fromValue<qulonglong>(m_model->shotListRevision())}
    };
}

void NativeFlowCalibrationSource::completeLoad()
{
    if (m_loadingRequestId.isEmpty() || !m_model) return;
    const QString id = m_loadingRequestId;
    const QString operation = m_loadingOperation;
    const int expectedIndex = m_requestedIndex;
    m_loadingRequestId.clear();
    m_loadingOperation.clear();
    m_requestedIndex = -1;
    setLoading(false);
    publish();
    if (expectedIndex >= 0 && m_model->currentShotIndex() != expectedIndex) {
        finish(id, operation, false, {}, QStringLiteral("conflict"));
        return;
    }
    const QVariantMap result = currentShot();
    const bool success = m_model->hasData();
    finish(id, operation, success, result,
           success ? QString() : (m_model->errorMessage().isEmpty() ? QStringLiteral("noShots") : m_model->errorMessage()));
}

void NativeFlowCalibrationSource::save(QString requestId, double multiplier,
                                        qint64 expectedCalibrationRevision)
{
    const QString operation = QStringLiteral("save");
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        emit operationCompleted(requestId, operation, false, {},
            start == ServiceRequestIds::Start::LimitReached ? QStringLiteral("tooManyRequests")
                                                            : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_model || !m_settings) { finish(requestId, operation, false, {}, QStringLiteral("unavailable")); return; }
    // Shot reads are shared work, but each client retains its own displayed
    // curves. Another client's navigation must not invalidate this settings edit.
    if (!FlowCalibrationMath::validSaveRequest(multiplier, expectedCalibrationRevision,
                                                m_calibrationRevision)) {
        const QString reason = expectedCalibrationRevision != m_calibrationRevision
            ? QStringLiteral("conflict") : QStringLiteral("invalidMultiplier");
        finish(requestId, operation, false, {}, reason);
        return;
    }
    m_model->setMultiplier(multiplier);
    m_model->save();
    const bool saved = qAbs(m_settings->flowCalibrationMultiplier() - multiplier) < 0.001;
    publish();
    finish(requestId, operation, saved,
           {{QStringLiteral("savedMultiplier"), m_settings->flowCalibrationMultiplier()}},
           saved ? QString() : QStringLiteral("saveFailed"));
}

void NativeFlowCalibrationSource::finish(const QString& requestId, const QString& operation,
                                           bool success, const QVariantMap& result,
                                           const QString& reason)
{
    if (!m_requests.finish(requestId)) return;
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeFlowCalibrationSource::setFailure(const QString& requestId, const QString& operation,
                                              const QString& reason)
{
    if (requestId.isEmpty()) return;
    finish(requestId, operation, false, {}, reason);
}
