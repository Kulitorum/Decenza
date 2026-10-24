#pragma once

#include "remoteservice.h"
#include "rep_shotuploadservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

class QQmlEngine;
class QJSEngine;
class QRemoteObjectNode;

class ShotUploadService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~ShotUploadService() override;
    static ShotUploadService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    QVariantMap snapshot() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    void setNode(QRemoteObjectNode* node) override;
    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {});
    Q_INVOKABLE void refresh();
signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result, const QString& reason);
    void recoveryProgress(const QString& requestId, int total, int imported, int skipped, int failed);
    void recoveryComplete(const QString& requestId, int total, int imported, int skipped, int failed);
    void machineChoiceNeeded(const QString& requestId, const QStringList& serials, const QStringList& labels);
private:
    friend struct ShotUploadServiceForeign;
    explicit ShotUploadService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending(const QString& reason);
    QPointer<QRemoteObjectNode> m_node;
    QPointer<ShotUploadStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QHash<QString, QString> m_privateReceipts;
    QSet<QString> m_decentChoiceOwners;
};
