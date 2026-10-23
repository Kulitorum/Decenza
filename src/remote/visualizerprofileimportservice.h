#pragma once

#include "remoteservice.h"
#include "rep_visualizerprofileimportservice_replica.h"

#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;

class VisualizerProfileImportService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool importing READ importing NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool fetching READ fetching NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList sharedShots READ sharedShots NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool sharedShotsTruncated READ sharedShotsTruncated NOTIFY stateChanged FINAL)

public:
    ~VisualizerProfileImportService() override;

    bool ready() const;
    bool importing() const;
    bool fetching() const;
    QString lastError() const;
    QVariantList sharedShots() const;
    bool sharedShotsTruncated() const;

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {},
                                const QString& requestId = {});
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void fetchSharedShots();
    Q_INVOKABLE QString importFromShareCode(const QString& code);
    Q_INVOKABLE QString importFromShotId(const QString& shotId);
    Q_INVOKABLE QString importFromShotIdWithName(const QString& shotId, const QString& name);
    Q_INVOKABLE QString importSelectedShots(const QStringList& shotIds, bool overwrite);
    Q_INVOKABLE QString saveOverwrite(const QString& requestId = {});
    Q_INVOKABLE QString saveAsNew(const QString& requestId = {});
    Q_INVOKABLE QString saveWithNewName(const QString& name, const QString& requestId = {});
    Q_INVOKABLE QString cancelPending(const QString& requestId = {});

signals:
    void stateChanged();
    void readyChanged();
    void sharedShotsChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason);
    void duplicateFound(const QString& requestId, const QString& title);
    void operationRejected(const QString& requestId, const QString& operation,
                           const QString& reason);
    void importSuccess(const QString& title);
    void importFailed(const QString& reason);

private:
    friend struct VisualizerProfileImportServiceForeign;
    explicit VisualizerProfileImportService(QRemoteObjectNode* node, QObject* parent);

    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<VisualizerProfileImportStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QString m_duplicateRequestId;
    QString m_duplicateResolutionPendingId;
    QSet<QString> m_cancelledRequests;
};
