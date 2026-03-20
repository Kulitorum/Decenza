#pragma once

#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>
#include <QSet>

class McpSession : public QObject {
    Q_OBJECT
public:
    explicit McpSession(QObject* parent = nullptr)
        : QObject(parent)
        , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
        , m_created(QDateTime::currentDateTimeUtc())
        , m_lastActivity(QDateTime::currentDateTimeUtc())
    {}

    QString id() const { return m_id; }
    QDateTime created() const { return m_created; }
    QDateTime lastActivity() const { return m_lastActivity; }
    void touch() { m_lastActivity = QDateTime::currentDateTimeUtc(); }

    bool initialized() const { return m_initialized; }
    void setInitialized(bool v) { m_initialized = v; }

    QJsonObject clientCapabilities() const { return m_clientCapabilities; }
    void setClientCapabilities(const QJsonObject& caps) { m_clientCapabilities = caps; }

    // SSE stream socket (nullable — not all sessions have an active SSE connection)
    QTcpSocket* sseSocket() const { return m_sseSocket.data(); }
    void setSseSocket(QTcpSocket* socket) { m_sseSocket = socket; }

    // Resource subscriptions
    QSet<QString> subscribedResources() const { return m_subscribedResources; }
    void subscribe(const QString& uri) { m_subscribedResources.insert(uri); }
    void unsubscribe(const QString& uri) { m_subscribedResources.remove(uri); }

    // Rate limiting: count of control+settings calls in current window
    int controlCallCount() const { return m_controlCallCount; }
    void incrementControlCalls() { m_controlCallCount++; }
    void resetControlCalls() { m_controlCallCount = 0; }

private:
    QString m_id;
    QDateTime m_created;
    QDateTime m_lastActivity;
    bool m_initialized = false;
    QJsonObject m_clientCapabilities;
    QPointer<QTcpSocket> m_sseSocket;
    QSet<QString> m_subscribedResources;
    int m_controlCallCount = 0;
};
