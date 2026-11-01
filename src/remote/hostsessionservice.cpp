#include "hostsessionservice.h"
#include "hostsessionpayload.h"
#include "servicevalue.h"
#include "core/packageidentity.h"
#include "network/sessionappearance.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_node;
std::function<void(const QVariantMap&)> s_appearancePublisher;

bool validate(const QVariantMap& values, qint64 appearanceBytes,
              MachineUiSnapshot* machineUi, QString* error)
{
    if (!HostSessionPayload::fitsTelemetry(values, appearanceBytes)) {
        *error = QStringLiteral("Host session exceeds its payload limit");
        return false;
    }
    const QString hostId = values.value(QStringLiteral("hostId")).toString();
    const QUuid host(hostId);
    double sequence = 0.0;
    if (values.value(QStringLiteral("packageId")).toString() != QString::fromLatin1(decenzaPackageId())
        || host.isNull() || host.toString(QUuid::WithoutBraces) != hostId
        || !ServiceValue::finiteNumber(values.value(QStringLiteral("sequence")), &sequence)
        || sequence < 0 || sequence > 9007199254740991.0 || std::floor(sequence) != sequence) {
        *error = QStringLiteral("Host session identity is invalid; reload the packaged interface");
        return false;
    }
    if (values.value(QStringLiteral("machineUi")).metaType().id() != QMetaType::QVariantMap
        || !MachineUiSnapshot::parse(QJsonObject::fromVariantMap(
                values.value(QStringLiteral("machineUi")).toMap()), machineUi, error)) {
        if (error->isEmpty()) *error = QStringLiteral("Host session fields are invalid");
        return false;
    }
    double number = 0;
    int interval = 0;
    const QString mode = values.value(QStringLiteral("extractionAnnouncementMode")).toString();
    if (values.value(QStringLiteral("extractionAnnouncementMode")).metaType().id() != QMetaType::QString
        || !ServiceValue::finiteNumber(values.value(QStringLiteral("pressureBar")), &number)
        || !ServiceValue::finiteNumber(values.value(QStringLiteral("flowMlPerSecond")), &number)
        || values.value(QStringLiteral("accessibilityEnabled")).metaType().id() != QMetaType::Bool
        || values.value(QStringLiteral("extractionAnnouncementsEnabled")).metaType().id() != QMetaType::Bool
        || !ServiceValue::boundedInteger(values.value(QStringLiteral("extractionAnnouncementInterval")), 5, 30, &interval)
        || (mode != QStringLiteral("timed") && mode != QStringLiteral("milestones_only")
            && mode != QStringLiteral("both"))) {
        *error = QStringLiteral("Host telemetry or accessibility settings are invalid");
        return false;
    }
    return true;
}
}

HostSessionService::HostSessionService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

HostSessionService* HostSessionService::create(QQmlEngine* engine, QJSEngine*)
{
    auto* service = new HostSessionService(s_node, engine);
    QJSEngine::setObjectOwnership(service, QJSEngine::CppOwnership);
    return service;
}

void HostSessionService::setPublishedNode(QRemoteObjectNode* node) { s_node = node; }

void HostSessionService::setAppearancePublisher(std::function<void(const QVariantMap&)> publisher)
{ s_appearancePublisher = std::move(publisher); }

void HostSessionService::publishAppearance(const QVariantMap& appearance)
{
    if (s_node && m_node == s_node && s_appearancePublisher)
        s_appearancePublisher(appearance);
}

void HostSessionService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { invalidateAppearance(); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &HostSessionService::update);
    connect(m_state, &QRemoteObjectReplica::notified, this, &HostSessionService::update);
    connect(m_state, &HostSessionStateReplica::appearanceSnapshotChanged, this, [this] {
        invalidateAppearance();
        update();
    });
    connect(m_state, &HostSessionStateReplica::snapshotChanged, this, &HostSessionService::update);
    connect(m_state, &HostSessionStateReplica::readyChanged, this, &HostSessionService::update);
    update();
}

void HostSessionService::invalidateAppearance()
{
    m_cachedAppearance.clear();
    m_appearanceHostId.clear();
    m_appearanceBytes = -1;
}

void HostSessionService::update()
{
    m_error.clear();
    const QVariantMap values = m_state ? m_state->snapshot() : QVariantMap();
    const bool published = RemoteServiceReplica::isReady(m_node, m_state, s_node) && m_state->ready();
    const QString host = values.value(QStringLiteral("hostId")).toString();
    if (published && (m_appearanceBytes < 0 || host != m_appearanceHostId)) {
        const auto appearance = m_state->appearanceSnapshot();
        const auto bytes = HostSessionPayload::appearanceSize(appearance);
        if (bytes < 0) {
            m_error = QStringLiteral("Host appearance exceeds its payload limit");
        } else if (SessionAppearance::validate(appearance, &m_cachedAppearance, &m_error)) {
            m_appearanceBytes = bytes;
            m_appearanceHostId = host;
        }
    }
    m_valid = published && m_error.isEmpty()
        && validate(values, m_appearanceBytes, &m_machineUi, &m_error);
    m_snapshot = m_valid ? values : QVariantMap();
    if (m_valid) {
        m_snapshot.insert(QStringLiteral("appearance"), m_cachedAppearance);
    } else {
        m_machineUi = {};
        invalidateAppearance();
    }
    emit stateChanged();
}
