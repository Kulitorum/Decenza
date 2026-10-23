#include "recipecontextservice.h"

#include <QMetaObject>
#include <QJSEngine>
#include <QQmlEngine>
#include <QUuid>

namespace {
QPointer<QRemoteObjectNode> s_recipeContextNode;
}

RecipeContextService* RecipeContextService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new RecipeContextService(s_recipeContextNode, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

RecipeContextService::RecipeContextService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

void RecipeContextService::setPublishedNode(QRemoteObjectNode* node)
{
    s_recipeContextNode = node;
}

void RecipeContextService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] {
                failPreferencePending(QStringLiteral("disconnected"));
                failPresetEditPending(QStringLiteral("disconnected"));
                failPending(QStringLiteral("disconnected"));
            }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, [this] {
        if (!connected()) {
            failPreferencePending(QStringLiteral("disconnected"));
            failPresetEditPending(QStringLiteral("disconnected"));
            failPending(QStringLiteral("disconnected"));
        }
        emit preferencesChanged();
        emit stateChanged();
    });
    connect(m_state, &QRemoteObjectReplica::notified, this, [this] {
        emit preferencesChanged();
        emit stateChanged();
    });
    connect(m_state, &RecipeContextStateReplica::preferencesChanged,
            this, &RecipeContextService::preferencesChanged);
    connect(m_state, &RecipeContextStateReplica::revisionChanged,
            this, &RecipeContextService::preferencesChanged);
    connect(m_state, &RecipeContextStateReplica::readyChanged,
            this, &RecipeContextService::stateChanged);
    connect(m_state, &RecipeContextStateReplica::historyReadyChanged,
            this, &RecipeContextService::stateChanged);
    connect(m_state, &RecipeContextStateReplica::preferenceCompleted, this,
            [this](const QString& requestId, const QString& name, bool success,
                   const QVariantMap& values, const QString& reason) {
        if (m_pendingPreferences.value(requestId) != name)
            return;
        m_pendingPreferences.remove(requestId);
        if (success && m_state)
            emit preferencesChanged();
        emit preferenceCompleted(requestId, name, success, values, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &RecipeContextStateReplica::presetEditCompleted, this,
            [this](const QString& requestId, const QString& domain, const QString& operation,
                   bool success, quint64 revision, const QVariantMap& values,
                   const QString& reason) {
        const auto it = m_pendingPresetEdits.constFind(requestId);
        if (it == m_pendingPresetEdits.cend() || it->first != domain || it->second != operation)
            return;
        m_pendingPresetEdits.remove(requestId);
        if (success)
            emit preferencesChanged();
        emit presetEditCompleted(requestId, domain, operation, success, revision, values, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &RecipeContextStateReplica::shotBaselineCompleted, this,
            [this](const QString& id, bool success, quint64 revision, const QVariantMap& values, const QString& reason) {
        if (!m_pendingPreferences.contains(id))
            return;
        m_pendingPreferences.remove(id);
        if (success) emit preferencesChanged();
        emit shotBaselineCompleted(id, success, revision, values, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &RecipeContextStateReplica::historyCompleted, this,
            [this](const QString& requestId, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (m_pendingHistory.value(requestId) != operation)
            return;
        m_pendingHistory.remove(requestId);
        emit historyCompleted(requestId, operation, success, result, reason);
    }, Qt::QueuedConnection);
    emit preferencesChanged();
    emit stateChanged();
}

bool RecipeContextService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_recipeContextNode);
}

QVariantMap RecipeContextService::preferences() const
{
    return connected() ? m_state->preferences() : QVariantMap();
}

quint64 RecipeContextService::revision() const
{
    return connected() ? m_state->revision() : 0;
}

bool RecipeContextService::ready() const
{
    return connected() && m_state->ready();
}

bool RecipeContextService::historyReady() const
{
    return connected() && m_state->historyReady();
}

QString RecipeContextService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

void RecipeContextService::refresh()
{
    if (connected())
        m_state->refresh();
}

void RecipeContextService::setPreference(const QString& requestId, const QString& name,
                                         const QVariant& value, quint64 expectedRevision)
{
    if (requestId.isEmpty() || m_pendingPreferences.contains(requestId)
        || m_pendingPresetEdits.contains(requestId))
        return;
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, requestId, name] {
            emit preferenceCompleted(requestId, name, false, {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pendingPreferences.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, requestId, name] {
            emit preferenceCompleted(requestId, name, false, {}, QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return;
    }
    m_pendingPreferences.insert(requestId, name);
    m_state->setPreference(requestId, name, value, expectedRevision);
}

void RecipeContextService::executePresetEdit(const QString& requestId, const QString& domain,
                                             const QString& operation, const QVariantMap& fields,
                                             quint64 expectedRevision)
{
    if (requestId.isEmpty() || m_pendingPresetEdits.contains(requestId)
        || m_pendingPreferences.contains(requestId))
        return;
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, requestId, domain, operation] {
            emit presetEditCompleted(requestId, domain, operation, false, 0, {},
                                     QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pendingPresetEdits.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, requestId, domain, operation] {
            emit presetEditCompleted(requestId, domain, operation, false, 0, {},
                                     QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return;
    }
    m_pendingPresetEdits.insert(requestId, qMakePair(domain, operation));
    m_state->executePresetEdit(requestId, domain, operation, fields, expectedRevision);
}

void RecipeContextService::applyShotBaseline(const QString& requestId, const QVariantMap& fields,
                                             quint64 expectedRevision)
{
    if (requestId.isEmpty() || m_pendingPreferences.contains(requestId)
        || m_pendingPresetEdits.contains(requestId))
        return;
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit shotBaselineCompleted(requestId, false, 0, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pendingPreferences.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit shotBaselineCompleted(requestId, false, 0, {}, QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return;
    }
    m_pendingPreferences.insert(requestId, QStringLiteral("shotBaseline"));
    m_state->applyShotBaseline(requestId, fields, expectedRevision);
}

void RecipeContextService::requestHistory(const QString& requestId,
                                          const QString& operation,
                                          const QVariantMap& fields)
{
    if (requestId.isEmpty() || m_pendingHistory.contains(requestId))
        return;
    if (!connected() || (!historyReady() && operation != QLatin1String("grindContext"))) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit historyCompleted(requestId, operation, false, {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pendingHistory.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, requestId, operation] {
            emit historyCompleted(requestId, operation, false, {}, QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return;
    }
    m_pendingHistory.insert(requestId, operation);
    m_state->requestHistory(requestId, operation, fields);
}

void RecipeContextService::requestShotMetadataUpdate(const QString& requestId,
                                                     qint64 shotId,
                                                     const QVariantMap& metadata)
{
    if (requestId.isEmpty() || m_pendingHistory.contains(requestId))
        return;
    if (!connected() || !historyReady()) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit historyCompleted(requestId, QStringLiteral("updateShotMetadata"), false,
                                  {}, QStringLiteral("unavailable"));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_pendingHistory.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, requestId] {
            emit historyCompleted(requestId, QStringLiteral("updateShotMetadata"), false,
                                  {}, QStringLiteral("busy"));
        }, Qt::QueuedConnection);
        return;
    }
    m_pendingHistory.insert(requestId, QStringLiteral("updateShotMetadata"));
    m_state->requestShotMetadataUpdate(requestId, shotId, metadata);
}

void RecipeContextService::failPending(const QString& reason)
{
    const auto pending = m_pendingHistory;
    m_pendingHistory.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        emit historyCompleted(it.key(), it.value(), false, {}, reason);
    }
}

void RecipeContextService::failPreferencePending(const QString& reason)
{
    const auto pending = m_pendingPreferences;
    m_pendingPreferences.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        if (it.value() == QLatin1String("shotBaseline"))
            emit shotBaselineCompleted(it.key(), false, 0, {}, reason);
        else
            emit preferenceCompleted(it.key(), it.value(), false, {}, reason);
    }
}

void RecipeContextService::failPresetEditPending(const QString& reason)
{
    const auto pending = m_pendingPresetEdits;
    m_pendingPresetEdits.clear();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        emit presetEditCompleted(it.key(), it.value().first, it.value().second,
                                 false, 0, {}, reason);
    }
}
