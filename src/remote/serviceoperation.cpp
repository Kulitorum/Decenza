#include "serviceoperation.h"

#include <QRegularExpression>
#include <QUuid>
#include <utility>

QString ServiceOperation::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ServiceOperation::begin(const QString& operation, qint64 id,
                                const QString& uiId, bool ready)
{
    if (!ready || busy()) {
        const QString reason = busy() ? QStringLiteral("busy") : QStringLiteral("disconnected");
        const auto generation = m_generation;
        QMetaObject::invokeMethod(this, [=] { report(generation, uiId, operation, id, false, id, reason); },
                                  Qt::QueuedConnection);
        return {};
    }
    ++m_generation;
    m_wireId = newId();
    m_uiId = uiId;
    m_operation = operation;
    m_id = id;
    const QString wireId = m_wireId;
    clearError();
    emit busyChanged();
    // A signal handler may disconnect the facade while begin() is running.
    return m_wireId == wireId ? wireId : QString();
}

void ServiceOperation::finish(const QString& wireId, bool success, qint64 resultId,
                              const QString& reason, const QVariantMap& result)
{
    if (wireId.isEmpty() || wireId != m_wireId)
        return;
    const auto uiId = std::exchange(m_uiId, {});
    const auto operation = std::exchange(m_operation, {});
    const auto originalId = m_id;
    const auto generation = m_generation;
    m_wireId.clear();
    emit busyChanged();
    report(generation, uiId, operation, originalId, success, resultId, reason, result);
}

void ServiceOperation::disconnectPending()
{
    if (!busy()) return;
    const auto uiId = std::exchange(m_uiId, {});
    const auto operation = std::exchange(m_operation, {});
    const auto originalId = m_id;
    const auto generation = m_generation;
    m_wireId.clear();
    emit busyChanged();
    QMetaObject::invokeMethod(this, [=] {
        report(generation, uiId, operation, originalId, false, originalId,
               tr("Connection lost; the result is unknown. The action was not retried."));
    }, Qt::QueuedConnection);
}

void ServiceOperation::clearError()
{
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorChanged();
}

void ServiceOperation::report(quint64 generation, const QString& uiId, const QString& operation, qint64 originalId,
                              bool success, qint64 resultId, const QString& reason,
                              const QVariantMap& result)
{
    if (generation == m_generation && !success && m_error != reason) {
        m_error = reason;
        emit errorChanged();
    }
    emit completed(uiId, operation, originalId, success, resultId, reason, result);
}

ServiceRequestIds::Start ServiceRequestIds::begin(const QString& id)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9._:-]+$"));
    if (id.isEmpty() || id.size() > 128 || !allowed.match(id).hasMatch())
        return Start::InvalidId;
    if (m_pending.contains(id)) return Start::DuplicateId;
    if (m_pending.size() >= 128) return Start::LimitReached;
    m_pending.insert(id);
    return Start::Accepted;
}
