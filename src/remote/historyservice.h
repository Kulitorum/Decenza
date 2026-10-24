#pragma once

#include "rep_historyservice_replica.h"
#include "remoteservice.h"

#include <QHash>
#include <QRemoteObjectNode>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <type_traits>

class QRemoteObjectNode;

class QQmlEngine;
class QJSEngine;

class HistoryService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 totalShots READ totalShots NOTIFY historyChanged FINAL)
    Q_PROPERTY(qint64 lastSavedShotId READ lastSavedShotId NOTIFY historyChanged FINAL)
    Q_PROPERTY(quint64 historyRevision READ historyRevision NOTIFY historyChanged FINAL)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(quint64 preferencesRevision READ preferencesRevision NOTIFY preferencesChanged FINAL)

public:
    static HistoryService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    bool ready() const;
    qint64 totalShots() const;
    qint64 lastSavedShotId() const;
    quint64 historyRevision() const;
    QVariantMap preferences() const;
    quint64 preferencesRevision() const;
    void setNode(QRemoteObjectNode* node) override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString requestAutoFavorites(const QString& groupBy, int maxItems);
    Q_INVOKABLE QString requestAutoFavoriteGroupDetails(const QVariantMap& group);
    Q_INVOKABLE QString requestMostRecentShotId();
    Q_INVOKABLE QString requestPage(const QVariantMap& filter, int offset, int limit);
    Q_INVOKABLE QString requestShot(qint64 shotId);
    Q_INVOKABLE QString requestOutcome(qint64 shotId);
    Q_INVOKABLE QString requestUploadState(qint64 shotId);
    Q_INVOKABLE QString requestDecentUploadState(qint64 shotId);
    Q_INVOKABLE QString requestBaristas();
    Q_INVOKABLE QString updateMetadata(qint64 shotId, const QVariantMap& metadata);
    Q_INVOKABLE QString requestUpdateShotMetadata(qint64 shotId, const QVariantMap& metadata);
    Q_INVOKABLE QString deleteShot(qint64 shotId);
    Q_INVOKABLE QString requestDeleteShot(qint64 shotId);
    Q_INVOKABLE QString deleteShots(const QVariantList& shotIds);
    Q_INVOKABLE QString requestShotOutcome(qint64 shotId);
    Q_INVOKABLE QString loadShotWithMetadata(qint64 shotId);
    Q_INVOKABLE QString setPreference(const QString& name, const QVariant& value,
                                      quint64 expectedRevision);

signals:
    void stateChanged();
    void historyChanged();
    void preferencesChanged();
    void pageReady(const QString& requestId, int offset, const QVariantList& results,
                   int totalCount, const QString& reason);
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QVariantMap& result,
                            const QString& reason);
    void historyDataChanged();
    void shotReady(qint64 shotId, const QVariantMap& shot);
    void shotOutcomeReady(qint64 shotId, const QVariantMap& outcome);
    void decentUploadStateReady(qint64 shotId, const QVariantMap& state);
    void shotMetadataUpdated(qint64 shotId, bool success);
    void shotDeleted(qint64 shotId);
    void shotBadgesUpdated(qint64 shotId, bool channelingDetected, bool grindIssueDetected,
                           bool skipFirstFrameDetected, bool pourTruncatedDetected);
    void shotPulledFromVisualizer(qint64 shotId, const QVariantMap& previous,
                                  const QVariantMap& written);
    void visualizerInfoUpdated(qint64 shotId, bool success);
    void decentUploadStateUpdated(qint64 shotId, bool success);

private:
    explicit HistoryService(QRemoteObjectNode* node, QObject* parent);
    bool connected() const;
    QString send(const QString& operation,
                 const std::function<void(HistoryStateReplica*, const QString&)>& request,
                 qint64 shotId = 0);
    void failPending(const QString& reason);
    void emitLegacyResult(const QString& operation, qint64 shotId,
                          const QVariantMap& result, bool success);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<HistoryStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QHash<QString, qint64> m_pendingShotIds;
};

static_assert(!std::is_default_constructible_v<HistoryService>,
              "The QML singleton must use its engine-owned factory.");
