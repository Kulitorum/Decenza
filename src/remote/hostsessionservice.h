#pragma once

#include "remoteservice.h"
#include "rep_hostsessionservice_replica.h"
#include "network/machineuisnapshot.h"
#include "network/steampitchersnapshot.h"

#include <QPointer>
#include <QVariantMap>
#include <functional>

class QQmlEngine;
class QJSEngine;

// Read-only host facts shared by the native and browser adapters.
class HostSessionService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
public:
    static HostSessionService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    static void setAppearancePublisher(std::function<void(const QVariantMap&)> publisher);
    Q_INVOKABLE void publishAppearance(const QVariantMap& appearance);
    void setNode(QRemoteObjectNode* node) override;
    bool ready() const { return m_valid; }
    QVariantMap snapshot() const { return m_snapshot; }
    QString errorMessage() const { return m_error; }
    QString packageId() const { return m_snapshot.value(QStringLiteral("packageId")).toString(); }
    QString hostId() const { return m_snapshot.value(QStringLiteral("hostId")).toString(); }
    quint64 sequence() const { return m_snapshot.value(QStringLiteral("sequence")).toULongLong(); }
    const MachineUiSnapshot& machineUi() const { return m_machineUi; }
    const SteamPitcherSnapshot& steamPitchers() const { return m_steamPitchers; }
    QVariantMap appearance() const { return m_snapshot.value(QStringLiteral("appearance")).toMap(); }
    double pressureBar() const { return m_snapshot.value(QStringLiteral("pressureBar")).toDouble(); }
    double flowMlPerSecond() const { return m_snapshot.value(QStringLiteral("flowMlPerSecond")).toDouble(); }
    bool accessibilityEnabled() const { return m_snapshot.value(QStringLiteral("accessibilityEnabled")).toBool(); }
    bool extractionAnnouncementsEnabled() const { return m_snapshot.value(QStringLiteral("extractionAnnouncementsEnabled"), true).toBool(); }
    int extractionAnnouncementInterval() const { return m_snapshot.value(QStringLiteral("extractionAnnouncementInterval"), 5).toInt(); }
    QString extractionAnnouncementMode() const { return m_snapshot.value(QStringLiteral("extractionAnnouncementMode"), QStringLiteral("both")).toString(); }

signals:
    void stateChanged();

private:
    friend struct HostSessionServiceForeign;
    explicit HostSessionService(QRemoteObjectNode* node, QObject* parent);
    void update();
    QPointer<QRemoteObjectNode> m_node;
    QPointer<HostSessionStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QVariantMap m_snapshot;
    MachineUiSnapshot m_machineUi;
    SteamPitcherSnapshot m_steamPitchers;
    QString m_error;
    bool m_valid = false;
};
