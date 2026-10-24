#pragma once

#include "remoteservice.h"
#include "rep_conversationsession_replica.h"

#include <QPointer>
#include <QHash>
#include <QVariantMap>

class QJSEngine;
class QQmlEngine;
class QRemoteObjectNode;

class ConversationSession final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString reportUrl READ reportUrl NOTIFY stateChanged FINAL)

public:
    ~ConversationSession() override;
    bool ready() const { return m_ready; }
    bool busy() const { return m_state.value(QStringLiteral("busy")).toBool(); }
    QString errorMessage() const { return m_errorMessage; }
    QVariantMap state() const { return m_state; }
    QString reportUrl() const;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static void setNativeSessionId(const QString& clientId);

    Q_INVOKABLE QString request(const QString& operation, const QVariantMap& fields = {});
    Q_INVOKABLE void refresh();

signals:
    void stateChanged();
    void completed(const QString& requestId, const QString& operation,
                   bool success, const QVariantMap& result, const QString& reason);

private:
    friend struct ConversationSessionForeign;
    explicit ConversationSession(QRemoteObjectNode* node, QObject* parent = nullptr);
    static QRemoteObjectNode* publishedNode();
    static QString nativeSessionId();
    bool connected() const;
    void receiveState(const QString& clientId, const QVariantMap& state);
    void receiveStateReady(const QString& clientId, const QString& requestId,
                           const QVariantMap& state);
    void receiveCompletion(const QString& clientId, const QString& requestId,
                           const QString& operation, bool success,
                           const QVariantMap& result, const QString& reason);
    QString send(const QString& operation, const QVariantMap& fields = {});

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ConversationSessionStateReplica> m_replica;
    QMetaObject::Connection m_nodeDestroyed;
    QString m_clientId;
    QVariantMap m_state;
    QString m_errorMessage;
    QHash<QString, QString> m_pending;
    bool m_ready = false;
    bool m_closing = false;
};
