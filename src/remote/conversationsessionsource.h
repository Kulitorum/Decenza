#pragma once

#include "rep_conversationsession_source.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QVariantMap>

class AIConversation;
class AIManager;
class ShotServer;
class ShotHistoryStorage;

class ConversationSessionSource final : public ConversationSessionStateSimpleSource
{
    Q_OBJECT
public:
    explicit ConversationSessionSource(AIManager* manager, ShotServer* shotServer,
                                       ShotHistoryStorage* shotHistory,
                                       QObject* parent = nullptr);

    // The native window keeps its existing AIConversation object. This binds
    // that object to the same RPC contract used by remote clients.
    void bindNativeSession(const QString& clientId);

    void requestState(QString clientId, QString requestId) override;
    void loadMostRecent(QString clientId, QString requestId) override;
    void openShot(QString clientId, QString requestId, QVariantMap shot,
                  qint64 shotId, QString shotLabel) override;
    void sendMessage(QString clientId, QString requestId, QString text) override;
    void retry(QString clientId, QString requestId) override;
    void clear(QString clientId, QString requestId) override;
    void dismissShot(QString clientId, QString requestId) override;
    void save(QString clientId, QString requestId) override;
    void closeClient(QString clientId) override;

private:
    struct Session {
        QPointer<AIConversation> conversation;
        AIConversation* conversationIdentity = nullptr;
        QVariantMap shot;
        qint64 shotId = 0;
        QString shotLabel;
        QString beverageType = QStringLiteral("espresso");
        QString pendingShotSummary;
        bool isMistakeShot = false;
        bool contextLoading = false;
        bool tasteIntakeEnabled = false;
        bool native = false;
        bool closeWhenIdle = false;
        quint64 contextGeneration = 0;
        QPointer<AIConversation> contextConversation;
        QString contextRequestId;
        QString contextRequestOperation;
    };
    struct PendingShotLoad {
        QString clientId;
        QString requestId;
        QPointer<AIConversation> conversation;
        quint64 generation = 0;
        qint64 shotId = 0;
        QString shotLabel;
        QVariantMap overrides;
        bool tasteIntakeEnabled = false;
    };

    Session* ensureSession(const QString& clientId);
    void attachConversation(const QString& clientId, AIConversation* conversation);
    QVariantMap stateFor(const Session& session) const;
    void publishState(const QString& clientId);
    void finish(const QString& clientId, const QString& requestId,
                const QString& operation, bool success,
                const QVariantMap& result = {}, const QString& reason = {});
    void fail(const QString& clientId, const QString& requestId,
              const QString& operation, const QString& reason);
    void contextReady(AIConversation* conversation);
    void storedShotReady(const QString& internalRequestId, qint64 shotId,
                         const QVariantMap& shot, const QString& reason);
    void prepareShotConversation(const QString& clientId, const QString& requestId,
                                Session& session, QVariantMap shot, qint64 shotId,
                                QString shotLabel, bool tasteIntakeEnabled);
    void conversationChanged(AIConversation* conversation);
    void maybeCloseSession(const QString& clientId);
    void forgetShotLoads(const QString& clientId);
    bool beginRequest(const QString& clientId, const QString& requestId,
                      const QString& operation);
    void endRequest(const QString& clientId, const QString& requestId);

    QPointer<AIManager> m_manager;
    QPointer<ShotServer> m_shotServer;
    QPointer<ShotHistoryStorage> m_shotHistory;
    QString m_nativeSessionId;
    QHash<QString, Session> m_sessions;
    QHash<AIConversation*, QString> m_clientByConversation;
    QHash<QString, QSet<QString>> m_pendingRequestIds;
    QHash<QString, PendingShotLoad> m_pendingShotLoads;
    static constexpr int MaxRemoteSessions = 8;
    static constexpr int MaxPendingRequestsPerClient = 32;
};
