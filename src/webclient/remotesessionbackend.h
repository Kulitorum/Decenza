#pragma once

#include "remote/hostsessionservice.h"
#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

class RemoteSessionBackend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(HostSessionService* service READ service WRITE setService NOTIFY serviceChanged FINAL)
    Q_PROPERTY(QString apiOrigin READ apiOrigin WRITE setApiOrigin NOTIFY apiOriginChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged FINAL)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool stale READ stale NOTIFY staleChanged FINAL)
    Q_PROPERTY(bool reloadRequired READ reloadRequired NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString hostId READ hostId NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(qint64 sequence READ sequence NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QString machineState READ machineState NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QString phase READ phase NOTIFY snapshotChanged FINAL)
    Q_PROPERTY(QVariantMap appearance READ appearance NOTIFY appearanceChanged FINAL)
    Q_PROPERTY(QVariantMap backgroundPattern READ backgroundPattern NOTIFY appearanceChanged FINAL)
public:
    explicit RemoteSessionBackend(QObject* parent = nullptr);
    HostSessionService* service() const { return m_service; }
    void setService(HostSessionService* service);
    QString apiOrigin() const { return m_apiOrigin; }
    void setApiOrigin(const QString& origin);
    QString status() const { return m_status; }
    QString errorMessage() const { return m_error; }
    bool connected() const { return m_connected; }
    bool stale() const { return m_stale; }
    bool busy() const { return m_status == QStringLiteral("connecting") || m_status == QStringLiteral("synchronizing"); }
    bool reloadRequired() const { return m_status == QStringLiteral("reload-required"); }
    bool connectionWanted() const { return m_wanted; }
    quint64 connectionGeneration() const { return m_generation; }
    QString expectedPackageId() const { return m_expectedPackage; }
    QString expectedHostId() const { return m_expectedHost; }
    QString hostId() const { return connected() && !m_stale && m_service ? m_service->hostId() : QString(); }
    qint64 sequence() const { return m_sequence; }
    const MachineUiSnapshot& machineUi() const;
    const SteamPitcherSnapshot& steamPitchers() const;
    bool accessibilityEnabled() const { return m_connected && !m_stale && m_service && m_service->accessibilityEnabled(); }
    bool extractionAnnouncementsEnabled() const { return m_connected && !m_stale && m_service ? m_service->extractionAnnouncementsEnabled() : true; }
    int extractionAnnouncementInterval() const { return m_connected && !m_stale && m_service ? m_service->extractionAnnouncementInterval() : 5; }
    QString extractionAnnouncementMode() const { return m_connected && !m_stale && m_service ? m_service->extractionAnnouncementMode() : QStringLiteral("both"); }
    double pressureBar() const { return m_connected && !m_stale && m_service ? m_service->pressureBar() : 0.0; }
    double flowMlPerSecond() const { return m_connected && !m_stale && m_service ? m_service->flowMlPerSecond() : 0.0; }
    QString machineState() const { return machineUi().stateString; }
    QString phase() const;
    QVariantMap appearance() const { return m_connected && !m_stale && m_service ? m_service->appearance() : QVariantMap(); }
    QVariantMap backgroundPattern() const;
    void setBootstrapIdentity(const QString& packageId, const QString& hostId);
    void transportOpened();
    void transportFailed(const QString& error, bool reloadRequired = false);
    Q_INVOKABLE void connectToHost();
    Q_INVOKABLE void disconnectFromHost();
    Q_INVOKABLE void reloadInterface();
signals:
    void serviceChanged();
    void apiOriginChanged();
    void statusChanged();
    void errorMessageChanged();
    void connectedChanged();
    void staleChanged();
    void snapshotChanged();
    void appearanceChanged();
    void connectionRequested();
private:
    void refresh();
    void invalidate();
    void setStatus(const QString& status);
    void setError(const QString& error);
    QPointer<HostSessionService> m_service;
    QMetaObject::Connection m_serviceChanged;
    QMetaObject::Connection m_serviceDestroyed;
    QString m_apiOrigin;
    QString m_expectedPackage;
    QString m_expectedHost;
    QString m_status = QStringLiteral("idle");
    QString m_error;
    QVariantMap m_lastAppearance;
    QTimer m_livenessTimer;
    QElapsedTimer m_lastSnapshotAt;
    qint64 m_sequence = -1;
    quint64 m_generation = 0;
    bool m_wanted = false;
    bool m_connected = false;
    bool m_stale = true;
};
