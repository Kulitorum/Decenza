#pragma once
#include "remoteservice.h"

#include "rep_profileservice_replica.h"
#include "serviceoperation.h"

#include <QObject>
#include <QPointer>
#include <QHash>

class QRemoteObjectNode;

class ProfileService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 catalogRevision READ catalogRevision NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList allProfilesList READ allProfilesList NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList availableProfiles READ availableProfiles NOTIFY stateChanged FINAL)
    Q_PROPERTY(QStringList installedProfileTitles READ installedProfileTitles NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString baseProfileName READ baseProfileName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString previousProfileName READ previousProfileName NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool profileModified READ profileModified NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isCurrentProfileReadOnly READ isCurrentProfileReadOnly NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap profileUsage READ profileUsage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList favoriteProfiles READ favoriteProfiles NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString favoriteProfileOrder READ favoriteProfileOrder NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString autoLoadProfileFilename READ autoLoadProfileFilename NOTIFY stateChanged FINAL)
    Q_PROPERTY(int autoLoadRevertMinutes READ autoLoadRevertMinutes NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap currentProfileSummary READ currentProfileSummary NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap profileSummaries READ profileSummaries NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap roastAffinities READ roastAffinities NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap grindDirections READ grindDirections NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap flowCalibration READ flowCalibration NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged FINAL)

public:
    ~ProfileService() override;
    bool ready() const;
    bool loading() const;
    QString errorMessage() const;
    qint64 catalogRevision() const;
    QVariantList allProfilesList() const;
    QVariantList availableProfiles() const;
    QStringList installedProfileTitles() const;
    QString baseProfileName() const;
    QString previousProfileName() const;
    bool profileModified() const;
    bool isCurrentProfileReadOnly() const;
    QVariantMap profileUsage() const;
    QVariantList favoriteProfiles() const;
    QString favoriteProfileOrder() const;
    QString autoLoadProfileFilename() const;
    int autoLoadRevertMinutes() const;
    QVariantMap profileSummaries() const;
    QVariantMap currentProfileSummary() const;
    QVariantMap roastAffinities() const;
    QVariantMap grindDirections() const;
    QVariantMap flowCalibration() const;
    bool busy() const { return m_operation.busy(); }
    QString operationError() const { return m_operation.error(); }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString requestProfileDetail(const QString& filename);
    Q_INVOKABLE QString requestCurrentProfile() { return requestProfileDetail({}); }
    Q_INVOKABLE QString saveDraft(const QString& filename, const QVariantMap& editedMap,
                                  const QString& expectedSourceRevision, const QString& saveAsTitle = {},
                                  const QString& overwriteRevision = {});
    Q_INVOKABLE QString tryDraft(const QString& filename, const QVariantMap& editedMap,
                                 const QString& expectedSourceRevision);
    Q_INVOKABLE QString requestKnowledge(const QString& title, const QString& profileJson = {});
    Q_INVOKABLE QVariantMap cachedProfileByFilename(const QString& filename) const;
    Q_INVOKABLE QString findProfileByTitle(const QString& title) const;
    Q_INVOKABLE QVariantMap profileCatalogInfoForTitle(const QString& title) const;
    Q_INVOKABLE QString beverageBucketForTitle(const QString& title) const;
    Q_INVOKABLE QVariantMap profileSummaryForTitle(const QString& title) const;
    Q_INVOKABLE bool profileHasKnowledge(const QString& title) const;
    Q_INVOKABLE QVariantList filterProfiles(const QVariantList& entries, const QVariantMap& chips,
                                            const QString& search, const QStringList& allowedBeverageTypes,
                                            const QVariantList& favoriteRows) const;
    Q_INVOKABLE QVariantMap facetCounts(const QVariantList& entries, const QVariantMap& chips,
                                        const QString& search, const QStringList& allowedBeverageTypes,
                                        const QVariantList& favoriteRows) const;
    Q_INVOKABLE QVariantList sortedByRecentUse(const QVariantList& entries,
                                               const QString& pinnedFilename, const QVariantMap& usage) const;
    Q_INVOKABLE bool teaProfileMatchesType(const QString& title, const QString& teaType) const;
    Q_INVOKABLE double defaultTeaTempC(const QString& teaType) const;
    Q_INVOKABLE bool kbProfileSuitsRoast(const QString& title, const QString& roastLevel) const;
    Q_INVOKABLE QString grindDirectionBetween(const QString& sourceTitle,
                                               const QString& targetTitle) const;
    Q_INVOKABLE bool isFavoriteProfile(const QString& filename) const;
    Q_INVOKABLE QString execute(const QString& operation, const QString& filename = {},
                                const QVariantMap& fields = {});
    Q_INVOKABLE QString loadProfile(const QString& filename);
    Q_INVOKABLE QString deleteProfile(const QString& filename);
    Q_INVOKABLE QString duplicateProfile(const QString& filename, const QString& newTitle);
    Q_INVOKABLE QString renameProfile(const QString& filename, const QString& newTitle);
    Q_INVOKABLE QString toggleFavoriteProfile(const QString& filename);
    Q_INVOKABLE QString saveFavoritesOrder(const QStringList& filenames, const QString& mode);
    Q_INVOKABLE QString setAutoLoadProfileFilename(const QString& filename);
    Q_INVOKABLE QString setAutoLoadRevertMinutes(int minutes);

signals:
    void stateChanged();
    void busyChanged();
    void profileDetailReady(const QString& requestId, const QString& filename,
                            const QVariantMap& detail, const QString& reason);
    void knowledgeReady(const QString& requestId, const QString& title,
                        const QString& profileJson, const QVariantMap& payload,
                        const QString& reason);
    void operationCompleted(const QString& requestId, const QString& operation,
                            const QString& filename, bool success,
                            const QString& reason, const QVariantMap& result);

private:
    friend struct ProfileServiceForeign;
    explicit ProfileService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    QString send(const QString& operation, const QString& filename,
                 const QVariantMap& fields = {});
    void failPending();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ProfileStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceOperation m_operation;
    QHash<QString, QString> m_detailRequests;
    QHash<QString, QPair<QString, QString>> m_knowledgeRequests;
    QHash<QString, QString> m_operationFiles;
    QHash<QString, QVariantMap> m_detailCache;
    qint64 m_cachedRevision = -1;
};
