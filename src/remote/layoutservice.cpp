#include "layoutservice.h"

#include "core/layoutcatalog.h"
#include "core/layoutsnapshot.h"

#include <QRemoteObjectNode>
#include <QMetaObject>
#include <utility>

namespace { QPointer<QRemoteObjectNode> s_publishedNode; }

LayoutService::LayoutService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

LayoutService::~LayoutService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void LayoutService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* LayoutService::publishedNode() { return s_publishedNode; }

bool LayoutService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

bool LayoutService::ready() const { return connected() && m_ready; }

QString LayoutService::errorMessage() const
{
    if (!connected()) return tr("Layout settings are disconnected. Reconnect to refresh them.");
    return m_error;
}

void LayoutService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &LayoutService::updateFromReplica);
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &LayoutService::updateFromReplica);
    connect(m_state, &LayoutStateReplica::layoutConfigurationChanged,
            this, &LayoutService::updateFromReplica);
    connect(m_state, &LayoutStateReplica::revisionChanged,
            this, &LayoutService::updateFromReplica);
    connect(m_state, &LayoutStateReplica::readyChanged,
            this, &LayoutService::updateFromReplica);
    connect(m_state, &LayoutStateReplica::errorMessageChanged,
            this, &LayoutService::updateFromReplica);
    connect(m_state, &LayoutStateReplica::operationCompleted, this,
        [this](const QString& id, const QString& operation, bool success, qint64 revision,
               const QString& configuration, const QString& reason) {
            if (!m_pending.contains(id) || m_pending.value(id) != operation) return;
            m_pending.remove(id);
            if (revision >= m_revision && revision >= 0) {
                const bool configChanged = m_configuration != configuration;
                m_configuration = configuration;
                m_revision = revision;
                if (configChanged) emit layoutConfigurationChanged();
            }
            const QString nextError = success ? QString() : reason;
            if (m_error != nextError) m_error = nextError;
            emit stateChanged();
            emit operationCompleted(id, operation, success, m_revision,
                                    m_configuration, reason);
        }, Qt::QueuedConnection);
    updateFromReplica();
}

void LayoutService::updateFromReplica()
{
    if (!connected()) {
        if (m_ready || !m_configuration.isEmpty() || m_revision != -1 || !m_error.isEmpty()) {
            m_ready = false;
            m_configuration.clear();
            m_revision = -1;
            m_error.clear();
            emit layoutConfigurationChanged();
            emit stateChanged();
        }
        failPending(QStringLiteral("disconnected"));
        return;
    }
    const qint64 incomingRevision = m_state->revision();
    if (incomingRevision < m_revision) return;
    const QString configuration = m_state->layoutConfiguration();
    const bool configChanged = m_configuration != configuration;
    const bool changed = configChanged || m_revision != incomingRevision
        || m_ready != m_state->ready() || m_error != m_state->errorMessage();
    m_configuration = configuration;
    m_revision = incomingRevision;
    m_ready = m_state->ready();
    m_error = m_state->errorMessage();
    if (configChanged) emit layoutConfigurationChanged();
    if (changed) emit stateChanged();
}

void LayoutService::failPending(const QString& reason)
{
    auto pending = std::move(m_pending);
    m_pending.clear();
    if (!pending.isEmpty()) emit stateChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString id = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, id, operation, reason] {
            emit operationCompleted(id, operation, false, m_revision, m_configuration, reason);
        }, Qt::QueuedConnection);
    }
}

QString LayoutService::execute(const QString& operation, const QVariantMap& fields,
                               qint64 expectedRevision)
{
    const QString requestId = ServiceOperation::newId();
    if (!ready() || m_pending.size() >= 128) {
        const QString reason = ready() ? QStringLiteral("tooManyRequests") : QStringLiteral("disconnected");
        QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
            emit operationCompleted(requestId, operation, false, m_revision, m_configuration, reason);
        }, Qt::QueuedConnection);
        return requestId;
    }
    m_pending.insert(requestId, operation);
    emit stateChanged();
    const qint64 revision = expectedRevision == -1 ? m_revision : expectedRevision;
    m_state->execute(requestId, revision, operation, fields);
    return requestId;
}

QVariantList LayoutService::getZoneItems(const QString& zoneName) const
{ return LayoutSnapshot(m_configuration).zoneItems(zoneName); }
int LayoutService::getZoneYOffset(const QString& zoneName) const
{ return LayoutSnapshot(m_configuration).zoneYOffset(zoneName); }
double LayoutService::getZoneScale(const QString& zoneName) const
{ return LayoutSnapshot(m_configuration).zoneScale(zoneName); }
QString LayoutService::getZoneOption(const QString& zoneName, const QString& key,
                                     const QString& defaultValue) const
{ return LayoutSnapshot(m_configuration).zoneOption(zoneName, key, defaultValue); }
QVariantMap LayoutService::getItemProperties(const QString& itemId) const
{ return LayoutSnapshot(m_configuration).itemProperties(itemId); }
bool LayoutService::itemIsConfigured(const QString& itemId) const
{ return LayoutSnapshot(m_configuration).itemIsConfigured(itemId); }

bool LayoutService::typeHasOptions(const QString& type) { return LayoutCatalog::typeHasOptions(type); }
QStringList LayoutService::optionKeysForType(const QString& type) { return LayoutCatalog::optionKeysForType(type); }
QStringList LayoutService::zoneOptionValues(const QString& key) { return LayoutCatalog::zoneOptionValues(key); }
QJsonObject LayoutService::readoutCapabilitiesJson() { return LayoutCatalog::readoutCapabilitiesJson(); }
QString LayoutService::defaultDisplayModeForType(const QString& type) { return LayoutCatalog::defaultDisplayModeForType(type); }
QJsonObject LayoutService::displayModeDefaultsJson() { return LayoutCatalog::displayModeDefaultsJson(); }
QVariantMap LayoutService::sleepOptionDefaults() { return LayoutCatalog::sleepOptionDefaults(); }
QJsonObject LayoutService::sleepOptionDefaultsJson() { return LayoutCatalog::sleepOptionDefaultsJson(); }
QVariantList LayoutService::widgetCatalog() { return LayoutCatalog::widgetCatalog(); }
QVariantMap LayoutService::widgetChipNames() { return LayoutCatalog::widgetChipNames(); }
QVariantList LayoutService::widgetCategoryNames() { return LayoutCatalog::widgetCategoryNames(); }
QJsonObject LayoutService::widgetCatalogJson() { return LayoutCatalog::widgetCatalogJson(); }
QVariantList LayoutService::layoutActionCatalog() { return LayoutCatalog::layoutActionCatalog(); }
QVariantMap LayoutService::layoutActionLabels() { return LayoutCatalog::layoutActionLabels(); }
QJsonObject LayoutService::layoutActionCatalogJson() { return LayoutCatalog::layoutActionCatalogJson(); }
QString LayoutService::gestureReservedActionForType(const QString& type) { return LayoutCatalog::gestureReservedActionForType(type); }
bool LayoutService::typeSupportsGestureOverrides(const QString& type) { return LayoutCatalog::typeSupportsGestureOverrides(type); }
