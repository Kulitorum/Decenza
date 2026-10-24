#pragma once

#include "remoteservice.h"
#include "rep_settingspreferencesservice_replica.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRemoteObjectNode>
#include <QVariantMap>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class SettingsPreferencesService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(QVariantMap revisions READ revisions NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)

public:
    static SettingsPreferencesService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);

    QVariantMap preferences() const;
    QVariantMap revisions() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    QString errorMessage() const { return m_errorMessage; }
    void setNode(QRemoteObjectNode* node) override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshShotMapLocation();
    Q_INVOKABLE void openShotMapLocationSettings();
    Q_INVOKABLE void setPreference(const QString& requestId, const QString& domain,
                                   const QString& name, const QVariant& value,
                                   quint64 expectedRevision);

signals:
    void preferencesChanged();
    void stateChanged();
    void busyChanged();
    void preferenceCompleted(const QString& requestId, const QString& domain,
                             const QString& name, bool success, quint64 revision,
                             const QString& reason);

private:
    friend struct SettingsPreferencesServiceForeign;
    explicit SettingsPreferencesService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void finishPending(const QString& requestId, const QString& domain,
                       const QString& name, bool success, quint64 revision,
                       const QString& reason);
    void failPending(const QString& reason);

    struct PendingWrite {
        QString domain;
        QString name;
    };

    QPointer<QRemoteObjectNode> m_node;
    QPointer<SettingsPreferencesStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, PendingWrite> m_pending;
    QString m_errorMessage;
};

static_assert(!std::is_default_constructible_v<SettingsPreferencesService>,
              "The QML singleton must use its engine-owned factory.");
