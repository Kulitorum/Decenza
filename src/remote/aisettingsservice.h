#pragma once

#include "remoteservice.h"
#include "rep_aisettingsservice_replica.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRemoteObjectNode>
#include <QVariantMap>

class QQmlEngine;
class QJSEngine;

class AISettingsService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
public:
    static AISettingsService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    QVariantMap snapshot() const;
    bool ready() const;
    QString errorMessage() const { return m_errorMessage; }
    void setNode(QRemoteObjectNode* node) override;
    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString request(const QString& operation);

signals:
    void stateChanged();
    void completed(const QString& requestId, const QString& operation,
                   bool success, const QVariantMap& result, const QString& reason);

private:
    friend struct AISettingsServiceForeign;
    explicit AISettingsService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void finishPending(const QString& requestId, const QString& operation,
                       bool success, const QVariantMap& result, const QString& reason);
    void failPending(const QString& reason);
    QPointer<QRemoteObjectNode> m_node;
    QPointer<AISettingsStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QString m_errorMessage;
};
