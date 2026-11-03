#pragma once

#include "remoteservice.h"
#include "rep_settingsconnectionsservice_replica.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <functional>

class QRemoteObjectNode;
class QQmlEngine;
class QJSEngine;

// Shared host-owned adapter for the connection setup panel. It only exposes a
// bounded discovery/configuration projection and routes actions to the native
// managers; it does not implement transport discovery or connection policy.
class SettingsConnectionsService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap portalCalibration READ portalCalibration NOTIFY portalCalibrationChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)

public:
    ~SettingsConnectionsService() override;
    static SettingsConnectionsService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    QVariantMap snapshot() const;
    QVariantMap portalCalibration() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    QString errorMessage() const;

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    // Native platform share entry point. The browser uses the fixed host download route;
    // native builds keep the application's existing file-share behavior.
    static void setNativeLogShareHandler(std::function<void()> handler);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString resolveLogDownloadUrl(const QString& path) const;
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {});

signals:
    void portalCalibrationChanged();
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason);
    void manualWifiMdnsDiscovered(const QString& requestId, const QString& hostname, const QString& ip);
    void manualWifiMdnsProbeFinished(const QString& requestId);
    void manualWifiValidationFailed(const QString& requestId, const QString& hostnameOrIp);
    void manualWifiValidationSucceeded(const QString& requestId, const QString& hostnameOrIp);
    void manualWifiValidationCancelled(const QString& requestId);
    void activeHdsScaleChanged();

private:
    friend struct SettingsConnectionsServiceForeign;
    explicit SettingsConnectionsService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ConnectionsStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
};
