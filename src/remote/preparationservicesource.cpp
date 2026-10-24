#include "preparationservicesource.h"

#include "core/preparationstate.h"

#include <QMetaObject>
#include <QUuid>

NativePreparationServiceSource::NativePreparationServiceSource(PreparationState* preparation,
                                                               QObject* parent)
    : PreparationServiceStateSimpleSource(parent), m_preparation(preparation)
{
    setSourceId(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (preparation) {
        connect(preparation, &PreparationState::changed, this, &NativePreparationServiceSource::publish);
        connect(preparation, &PreparationState::availableChanged, this,
                &NativePreparationServiceSource::publish);
        connect(preparation, &QObject::destroyed, this, &NativePreparationServiceSource::publish);
    }
    publish();
}

void NativePreparationServiceSource::publish()
{
    setSnapshot(m_preparation ? m_preparation->snapshot().toJson().toVariantMap() : QVariantMap{});
    setReady(m_preparation && m_preparation->available());
}

void NativePreparationServiceSource::execute(QString requestId, QString action)
{
    if (QUuid::fromString(requestId).isNull()) {
        complete(requestId, action, false, tr("Invalid preparation request ID."));
        return;
    }
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId) return;
    if (start != ServiceRequestIds::Start::Accepted) {
        complete(requestId, action, false, tr("Too many preparation requests."));
        return;
    }
    bool success = false;
    QString message;
    if (!m_preparation || !m_preparation->available()) {
        message = tr("Preparation is unavailable.");
    } else if (PreparationSnapshot::isMode(action)) {
        success = m_preparation->selectMode(action);
    } else if (action == QLatin1String("milk-start")) {
        success = m_preparation->startMilkAttempt();
    } else if (action == QLatin1String("milk-cancel")) {
        success = m_preparation->cancelMilkAttempt();
    } else if (action == QLatin1String("milk-lock") || action == QLatin1String("milk-unlock")) {
        success = m_preparation->setMilkTimeLocked(action == QLatin1String("milk-lock"));
    } else {
        message = tr("Unknown preparation action.");
    }
    if (!success && message.isEmpty()) message = tr("The preparation action was refused by the host.");
    publish();
    complete(requestId, action, success, message);
}

void NativePreparationServiceSource::complete(const QString& requestId, const QString& action,
                                              bool success, const QString& message)
{
    QMetaObject::invokeMethod(this, [this, requestId, action, success, message] {
        m_requests.finish(requestId);
        emit operationCompleted(requestId, action, success, message);
    }, Qt::QueuedConnection);
}
