#include "beanuiservice.h"
#include "serviceoperation.h"
#include "core/beanimagekey.h"
#include "core/communityhelpers.h"
#include "network/beanbase_blob.h"
#include <QRemoteObjectNode>
#include <QUrl>
#include <utility>
namespace { QPointer<QRemoteObjectNode> s_node; }
BeanUiService::BeanUiService(QRemoteObjectNode* node, QObject* parent) : RemoteService(parent) { setNode(node); }
BeanUiService::~BeanUiService() { if (m_state) disconnect(m_state, nullptr, this, nullptr); }
void BeanUiService::setPublishedNode(QRemoteObjectNode* node) { s_node = node; }
QRemoteObjectNode* BeanUiService::publishedNode() { return s_node; }
bool BeanUiService::ready() const {
    return RemoteServiceReplica::isReady(m_node, m_state, s_node);
}
bool BeanUiService::isConfigured() const { return ready() && m_state->capabilities().value("configured").toBool(); }
QString BeanUiService::selectedProvider() const { return ready() ? m_state->capabilities().value("provider").toString() : QString(); }
bool BeanUiService::communityApiAvailable() const { return ready() && m_state->capabilities().value("communityApiAvailable").toBool(); }
bool BeanUiService::currentProviderSupportsWebSearch() const { return ready() && m_state->capabilities().value("webSearch").toBool(); }
bool BeanUiService::supportsProductPageSearch() const { return ready() && m_state->capabilities().value("search").toBool(); }
bool BeanUiService::supportsUrlExtraction() const { return ready() && m_state->capabilities().value("extractUrl").toBool(); }
void BeanUiService::setNode(QRemoteObjectNode* node) {
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
                                      [this] { failPending(); })) return;
    m_images.clear(); m_links.clear();
    const auto changed = [this] { if (!ready()) failPending(); emit stateChanged(); };
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, changed);
    connect(m_state, &BeanUiStateReplica::capabilitiesChanged, this, changed);
    connect(m_state, &BeanUiStateReplica::result, this, &BeanUiService::finish, Qt::QueuedConnection);
    connect(m_state, &BeanUiStateReplica::imageReady, this, [this](const QString& key, const QString& localUrl) {
        const auto url = m_node == s_node ? localUrl
            : resourceOrigin().resolved(QUrl(QStringLiteral("/api/session/bean-image/")
                + QString::fromLatin1(BeanImageKey::encodePathSegment(key)))).toString();
        if (!m_images.contains(key) && m_images.size() >= 1024) m_images.clear();
        m_images.insert(key, url);
        emit bagImageReady(key, url);
    });
    connect(m_state, &BeanUiStateReplica::linkResolved, this, [this](const QString& url, const QString& state) {
        if (!m_links.contains(url) && m_links.size() >= 1024) m_links.clear();
        m_links.insert(url, state); emit linkStateResolved(url, state);
    });
    connect(m_state, &BeanUiStateReplica::canonicalReady, this, &BeanUiService::canonicalDetails);
    emit stateChanged();
}
QString BeanUiService::send(const QString& operation, QVariantMap args, const QString& token) {
    const auto id = ServiceOperation::newId();
    if (m_pending.size() >= 128) {
        const Pending rejected{operation, token, {}};
        QMetaObject::invokeMethod(this, [this, id, rejected] {
            deliverFailure(id, rejected, tr("Too many bean requests are pending."));
        }, Qt::QueuedConnection);
        return id;
    }
    m_pending.insert(id, {operation, token, args.value(QStringLiteral("operationId")).toString()});
    if (ready()) m_state->invoke(id, operation, args);
    else QMetaObject::invokeMethod(this, [this,id,operation] { finish(id, operation, {}, QStringLiteral("disconnected")); }, Qt::QueuedConnection);
    return id;
}
void BeanUiService::finish(const QString& id, const QString& operation, const QVariantMap& values, const QString& reason) {
    const auto it = m_pending.find(id);
    if (it == m_pending.end() || it->operation != operation) return;
    const auto pending = it.value(); m_pending.erase(it);
    if (id == m_searchId) m_searchId.clear();
    if (!reason.isEmpty()) {
        deliverFailure(id, pending, reason);
        return;
    }
    if (operation == "communitySearch") {
        emit communityResultsReady(pending.token, values);
    } else if (operation == "communityProfile") {
        emit communityProfileReady(id, pending.token, values);
    } else if (operation == "researchBean") {
        emit beanResearchReady(pending.token, values.value(QStringLiteral("advice")).toString());
    } else if (operation == "applyCommunity") {
        emit communitySuggestionApplied(pending.token, values.value(QStringLiteral("ok")).toBool(),
            values.value(QStringLiteral("recipeName")).toString(), values.value(QStringLiteral("selected")).toBool());
    } else if (operation == "search") {
        if (reason.isEmpty()) emit searchResults(pending.token, values.value("entries").toList());
        else emit searchFailed(pending.token, reason);
    } else if (operation == "page") {
        if (reason.isEmpty()) emit pageTextReady(pending.token, values.value("text").toString(), id);
        else emit pageTextFailed(pending.token, reason, id);
    } else if (operation == "extract" || operation == "extractUrl") {
        if (reason.isEmpty()) emit bagDetailsExtracted(id, pending.token, values);
        else emit bagDetailsExtractionFailed(id, pending.token, reason);
    } else if (operation == "findPage") {
        if (reason.isEmpty()) emit productPageFound(pending.token, values.value("url").toString());
        else emit productPageSearchFailed(pending.token, reason);
    } else if (!reason.isEmpty()) emit operationFailed(reason);
}
void BeanUiService::deliverFailure(const QString& id, const Pending& pending, const QString& reason) {
    if (pending.operation == "communitySearch") emit communitySearchFailed(pending.token, reason);
    else if (pending.operation == "communityProfile") emit communityProfileFailed(id, pending.token, reason);
    else if (pending.operation == "researchBean") emit beanResearchFailed(pending.token, reason);
    else if (pending.operation == "applyCommunity") emit communitySuggestionApplied(pending.token, false, {}, false);
    else if (pending.operation == "search") emit searchFailed(pending.token, reason);
    else if (pending.operation == "page") emit pageTextFailed(pending.token, reason, id);
    else if (pending.operation == "extract" || pending.operation == "extractUrl") emit bagDetailsExtractionFailed(id, pending.token, reason);
    else if (pending.operation == "findPage") emit productPageSearchFailed(pending.token, reason);
    else emit operationFailed(reason);
}
void BeanUiService::failPending() {
    const auto pending = std::exchange(m_pending, QHash<QString, Pending>{});
    m_searchId.clear();
    for (auto it=pending.cbegin(); it!=pending.cend(); ++it) {
        const auto id = it.key();
        const auto request = it.value();
        QMetaObject::invokeMethod(this, [this, id, request] {
            deliverFailure(id, request, tr("Connection lost; the action was not retried."));
        }, Qt::QueuedConnection);
    }
}
void BeanUiService::search(const QString& query) {
    if (!m_searchId.isEmpty()) { if (ready()) m_state->cancel(m_searchId); m_pending.remove(m_searchId); }
    m_searchId = send("search", {{"query",query}}, query);
}
void BeanUiService::fetchCanonicalDetails(const QVariantMap& entry) { send("canonical",{{"entry",entry}}); }
QString BeanUiService::bagImageKey(qint64 bagId,const QString& id) const { return BeanImageKey::forBag(bagId,id); }
QString BeanUiService::bagImagePath(const QString& key) const { return m_images.value(key); }
void BeanUiService::ensureBagImage(const QString& key,const QString& name,const QString& url) { if (!key.isEmpty()) send("ensureImage",{{"key",key},{"name",name},{"url",url}}); }
void BeanUiService::refreshBagImage(const QString& key,const QString& name,const QString& url) { if (!key.isEmpty()) send("refreshImage",{{"key",key},{"name",name},{"url",url}}); }
void BeanUiService::replaceBagImageFromUrl(const QString& key,const QString& url) { if (!key.isEmpty()) send("replaceImage",{{"key",key},{"url",url}}); }
QString BeanUiService::linkState(const QString& url) const { return m_links.value(url,QStringLiteral("unknown")); }
void BeanUiService::probeLinkState(const QString& url) { send("probeLink",{{"url",url}}); }
QString BeanUiService::mergeEditorDetails(const QString& blob,const QVariantMap& details,const QString& roaster,const QString& coffee,const QString& roast) const { return BeanBaseBlob::mergeEditorDetails(blob,details,roaster,coffee,roast); }
QString BeanUiService::revertToCanonical(const QString& blob) const { return BeanBaseBlob::revertToCanonical(blob); }
bool BeanUiService::linkIsUsable(const QString& blob,const QString& link) const { return BeanBaseBlob::linkIsUsable(blob,link); }
bool BeanUiService::blobDiffersFromCanonical(const QString& blob) const { return BeanBaseBlob::differsFromCanonical(blob); }
QVariantMap BeanUiService::applyExtraction(const QString& blob,const QVariantMap& extracted,const QVariantMap& current) const {
    const auto out=BeanBaseBlob::applyExtraction(blob,extracted,current);
    return {{"blob",out.blob},{"applied",out.applied},{"corrections",out.corrections}};
}
QString BeanUiService::fetchPageText(const QString& url,qint64 bagId) { return send("page",{{"url",url},{"bagId",bagId}},url); }
void BeanUiService::abandonPageOperation(const QString& id) {
    if (id.isEmpty()) return;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->parentRequestId == id) it = m_pending.erase(it);
        else ++it;
    }
    m_pending.remove(id);
    if (m_searchId == id) m_searchId.clear();
    if (ready()) m_state->cancel(id);
}
void BeanUiService::abandonDiagnosticOperation(const QString& id) { abandonPageOperation(id); }
QString BeanUiService::extractCoffeeBagDetails(const QString& token,const QString& text,const QString& kind,const QString& operationId) { return send("extract",{{"text",text},{"kind",kind},{"operationId",operationId}},token); }
QString BeanUiService::extractCoffeeBagDetailsFromUrl(const QString& token,const QString& url,const QString& kind,const QString& operationId) { return send("extractUrl",{{"url",url},{"kind",kind},{"operationId",operationId}},token); }
QString BeanUiService::findProductPage(const QString& token,const QString& roaster,const QString& coffee,const QString& kind,qint64 bagId) { return send("findPage",{{"roaster",roaster},{"coffee",coffee},{"kind",kind},{"bagId",bagId}},token); }
void BeanUiService::logProductPageSearchDeclined(const QString& reason) { send("searchDeclined",{{"reason",reason}}); }
void BeanUiService::refreshVisualizerBag(qint64 bagId) { send("refreshVisualizer",{{"bagId",bagId}}); }
void BeanUiService::inspectBagLinks(qint64 bagId) { send("inspectBagLinks", {{"bagId", bagId}}); }
void BeanUiService::refreshVisualizerBags() { send("refreshVisualizerBags"); }

void BeanUiService::communitySearch(const QString& roaster, const QString& coffee, const QString& canonicalId)
{
    send(QStringLiteral("communitySearch"), {{"roaster", roaster}, {"coffee", coffee},
        {"canonicalId", canonicalId}}, communitySearchKey(roaster, coffee, canonicalId));
}
QString BeanUiService::fetchCommunityProfile(const QString& shotId)
{
    return send(QStringLiteral("communityProfile"), {{"shotId", shotId}}, shotId);
}
QString BeanUiService::researchBean(const QString& token, const QString& roaster,
                                   const QString& coffee, const QString& productUrl)
{
    return send(QStringLiteral("researchBean"), {{"roaster", roaster}, {"coffee", coffee},
        {"productUrl", productUrl}}, token);
}
void BeanUiService::applyCommunitySuggestion(const QVariantMap& suggestion)
{
    send(QStringLiteral("applyCommunity"), {{"suggestion", suggestion}},
         suggestion.value(QStringLiteral("key")).toString());
}
QUrl BeanUiService::communityPageUrl(const QString& roaster, const QString& coffee) const
{
    if (!ready()) return {};
    const auto capabilities = m_state->capabilities();
    return CommunityHelpers::pageUrl(capabilities.value(QStringLiteral("communityBaseUrl")).toString(),
        capabilities.value(QStringLiteral("communityMinEnjoyment")).toInt(), roaster, coffee);
}
QString BeanUiService::communitySearchKey(const QString& roaster, const QString& coffee,
                                         const QString& canonicalId) const
{
    return CommunityHelpers::searchKey(roaster, coffee, canonicalId);
}
QString BeanUiService::communityRecipeName(const QString& coffee, const QString& title) const
{
    return CommunityHelpers::recipeName(coffee, title);
}
