#pragma once

#include "remoteservice.h"
#include "rep_profileimportservice_replica.h"

#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;

class ProfileImportService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)

public:
    ~ProfileImportService() override;

    bool ready() const;
    QVariantMap snapshot() const;
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString execute(const QString& operation,
                                const QVariantMap& arguments = {});
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void scanProfiles();
    Q_INVOKABLE void scanTransfer(const QString& transferId);
    Q_INVOKABLE void importTransfer(const QString& transferId);
    Q_INVOKABLE void importById(const QString& profileId, bool force = false);
    Q_INVOKABLE void importAllNew();
    Q_INVOKABLE void updateAllDifferent();
    Q_INVOKABLE void saveOverwrite();
    Q_INVOKABLE void saveAsNew();
    Q_INVOKABLE void saveWithNewName(const QString& name);
    Q_INVOKABLE void cancelImport();

signals:
    void stateChanged();
    void readyChanged();
    void importSuccess(const QString& title);
    void importFailed(const QString& reason);
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason);
    void duplicateFound(const QString& title, const QString& existingPath);
    void operationRejected(const QString& requestId, const QString& operation,
                           const QString& reason);

private:
    friend struct ProfileImportServiceForeign;
    explicit ProfileImportService(QRemoteObjectNode* node, QObject* parent);

    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    void failPending();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ProfileImportStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QString m_duplicateRequestId;
};
