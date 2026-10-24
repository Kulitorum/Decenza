#pragma once
#include "rep_beanuiservice_replica.h"
#include "remoteservice.h"
#include <QHash>
#include <QPointer>
#include <QUrl>
class QRemoteObjectNode;

// Bean editor operations use the same host services in native and Wasm.
// Pure blob transformations stay local and shared; network/AI work stays native.
class BeanUiService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isConfigured READ isConfigured NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString selectedProvider READ selectedProvider NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool communityApiAvailable READ communityApiAvailable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool currentProviderSupportsWebSearch READ currentProviderSupportsWebSearch NOTIFY stateChanged FINAL)
public:
    ~BeanUiService() override;
    static void setPublishedNode(QRemoteObjectNode* node);
    void setNode(QRemoteObjectNode* node) override;
    bool ready() const;
    bool isConfigured() const;
    QString selectedProvider() const;
    bool communityApiAvailable() const;
    bool currentProviderSupportsWebSearch() const;
    Q_INVOKABLE void communitySearch(const QString& roaster, const QString& coffee, const QString& canonicalId);
    Q_INVOKABLE QString fetchCommunityProfile(const QString& shotId);
    Q_INVOKABLE QString researchBean(const QString& token, const QString& roaster,
                                     const QString& coffee, const QString& productUrl);
    Q_INVOKABLE void applyCommunitySuggestion(const QVariantMap& suggestion);
    Q_INVOKABLE QUrl communityPageUrl(const QString& roaster, const QString& coffee) const;
    Q_INVOKABLE QString communitySearchKey(const QString& roaster, const QString& coffee,
                                           const QString& canonicalId) const;
    Q_INVOKABLE QString communityRecipeName(const QString& coffee, const QString& title) const;
    Q_INVOKABLE bool supportsProductPageSearch() const;
    Q_INVOKABLE bool supportsUrlExtraction() const;
    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void fetchCanonicalDetails(const QVariantMap& entry);
    Q_INVOKABLE QString bagImageKey(qint64 bagId, const QString& canonicalId) const;
    // Returns a URL in both environments. Callers must not prepend file://.
    Q_INVOKABLE QString bagImagePath(const QString& key) const;
    Q_INVOKABLE void ensureBagImage(const QString& key, const QString& name, const QString& url);
    Q_INVOKABLE void refreshBagImage(const QString& key, const QString& name, const QString& url);
    Q_INVOKABLE void replaceBagImageFromUrl(const QString& key, const QString& url);
    Q_INVOKABLE QString linkState(const QString& url) const;
    Q_INVOKABLE void probeLinkState(const QString& url);
    Q_INVOKABLE QString mergeEditorDetails(const QString& blob, const QVariantMap& details,
        const QString& roaster, const QString& coffee, const QString& roast) const;
    Q_INVOKABLE QString revertToCanonical(const QString& blob) const;
    Q_INVOKABLE bool linkIsUsable(const QString& blob, const QString& link) const;
    Q_INVOKABLE bool blobDiffersFromCanonical(const QString& blob) const;
    Q_INVOKABLE QVariantMap applyExtraction(const QString& blob, const QVariantMap& extracted,
        const QVariantMap& current) const;
    Q_INVOKABLE QString fetchPageText(const QString& url, qint64 bagId = 0);
    Q_INVOKABLE void abandonPageOperation(const QString& id);
    Q_INVOKABLE void abandonDiagnosticOperation(const QString& id);
    Q_INVOKABLE QString extractCoffeeBagDetails(const QString& token, const QString& text,
        const QString& kind = QStringLiteral("coffee"), const QString& operationId = {});
    Q_INVOKABLE QString extractCoffeeBagDetailsFromUrl(const QString& token, const QString& url,
        const QString& kind = QStringLiteral("coffee"), const QString& operationId = {});
    Q_INVOKABLE QString findProductPage(const QString& token, const QString& roaster,
        const QString& coffee, const QString& kind, qint64 bagId = 0);
    Q_INVOKABLE void logProductPageSearchDeclined(const QString& reason);
    Q_INVOKABLE void refreshVisualizerBag(qint64 bagId);
    // Starts host-owned recovery/validation for a stored bag. The service
    // re-reads the bag and commits link results with a raw-blob compare guard.
    Q_INVOKABLE void inspectBagLinks(qint64 bagId);
    Q_INVOKABLE void refreshVisualizerBags();
signals:
    void stateChanged();
    void communityResultsReady(const QString& key, const QVariantMap& summary);
    void communitySearchFailed(const QString& key, const QString& status);
    void communityProfileReady(const QString& requestId, const QString& shotId, const QVariantMap& result);
    void communityProfileFailed(const QString& requestId, const QString& shotId, const QString& status);
    void beanResearchReady(const QString& token, const QString& advice);
    void beanResearchFailed(const QString& token, const QString& error);
    void communitySuggestionApplied(const QString& key, bool ok, const QString& recipeName, bool selected);
    void searchResults(const QString& query, const QVariantList& entries);
    void searchFailed(const QString& query, const QString& reason);
    void canonicalDetails(const QString& id, const QVariantMap& attributes);
    void bagImageReady(const QString& key, const QString& url);
    void linkStateResolved(const QString& url, const QString& state);
    void pageTextReady(const QString& url, const QString& text, const QString& operationId);
    void pageTextFailed(const QString& url, const QString& reason, const QString& operationId);
    void bagDetailsExtracted(const QString& requestId, const QString& token, const QVariantMap& fields);
    void bagDetailsExtractionFailed(const QString& requestId, const QString& token, const QString& reason);
    void productPageFound(const QString& token, const QString& url);
    void productPageSearchFailed(const QString& token, const QString& reason);
    void operationFailed(const QString& reason);
private:
    struct Pending { QString operation; QString token; QString parentRequestId; };
    friend struct BeanUiServiceForeign;
    explicit BeanUiService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    QString send(const QString& operation, QVariantMap args = {}, const QString& token = {});
    void finish(const QString& id, const QString& operation, const QVariantMap& values, const QString& reason);
    void failPending();
    void deliverFailure(const QString& id, const Pending& pending, const QString& reason);
    QPointer<QRemoteObjectNode> m_node;
    QPointer<BeanUiStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, Pending> m_pending;
    QHash<QString, QString> m_images, m_links;
    QString m_searchId;
};
