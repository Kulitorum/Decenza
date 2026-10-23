#pragma once

#include "remoteservice.h"
#include "rep_debugconversionservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class DebugConversionService final : public RemoteService
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
public:
    ~DebugConversionService() override;
    bool ready() const;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static QRemoteObjectNode* publishedNode();
    static DebugConversionService* create(QQmlEngine* engine, QJSEngine* jsEngine);

    Q_INVOKABLE QString execute(const QString& operation, const QVariantMap& arguments = {},
                                const QString& requestId = {});

signals:
    void stateChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result, const QString& reason);
    void operationProgress(const QString& requestId, const QVariantMap& progress);

private:
    friend struct DebugConversionServiceForeign;
    explicit DebugConversionService(QRemoteObjectNode* node, QObject* parent);
    bool connected() const;
    void observeConnectionState();
    void finishPending(const QString& requestId, const QString& operation, bool success,
                       const QVariantMap& result, const QString& reason);
    void relayProgress(const QString& requestId, const QVariantMap& progress);
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<DebugConversionStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
};

static_assert(!std::is_default_constructible_v<DebugConversionService>);
