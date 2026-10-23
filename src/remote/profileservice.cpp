#include "profileservice.h"

#include "../core/drinktypes.h"
#include "../core/profilecataloglogic.h"
#include "../profile/beveragetype.h"

#include <QRemoteObjectNode>
#include <QUuid>
#include <utility>

namespace { QPointer<QRemoteObjectNode> s_profileNode; }

ProfileService::ProfileService(QRemoteObjectNode* node, QObject* parent) : RemoteService(parent)
{
    connect(&m_operation, &ServiceOperation::busyChanged, this, &ProfileService::busyChanged);
    connect(&m_operation, &ServiceOperation::errorChanged, this, &ProfileService::stateChanged);
    connect(&m_operation, &ServiceOperation::completed, this,
        [this](const QString& requestId, const QString& operation, qint64,
               bool success, qint64, const QString& reason, const QVariantMap& result) {
            emit operationCompleted(requestId, operation, m_operationFiles.take(requestId),
                                    success, reason, result);
        });
    setNode(node);
}

ProfileService::~ProfileService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void ProfileService::setPublishedNode(QRemoteObjectNode* node) { s_profileNode = node; }
QRemoteObjectNode* ProfileService::publishedNode() { return s_profileNode; }

bool ProfileService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_profileNode);
}
bool ProfileService::ready() const { return connected() && m_state->ready(); }
bool ProfileService::loading() const { return connected() && m_state->loading(); }
QString ProfileService::errorMessage() const
{
    return connected() ? m_state->errorMessage()
        : tr("Profiles are disconnected. Reconnect to refresh their state.");
}
qint64 ProfileService::catalogRevision() const { return connected() ? m_state->catalogRevision() : -1; }
QVariantList ProfileService::allProfilesList() const { return connected() ? m_state->allProfilesList() : QVariantList(); }
QVariantList ProfileService::availableProfiles() const { return connected() ? m_state->availableProfiles() : QVariantList(); }
QStringList ProfileService::installedProfileTitles() const { return connected() ? m_state->installedProfileTitles() : QStringList(); }
QString ProfileService::baseProfileName() const { return connected() ? m_state->baseProfileName() : QString(); }
QString ProfileService::previousProfileName() const { return connected() ? m_state->previousProfileName() : QString(); }
bool ProfileService::profileModified() const { return connected() && m_state->profileModified(); }
bool ProfileService::isCurrentProfileReadOnly() const { return connected() && m_state->currentProfileReadOnly(); }
QVariantMap ProfileService::profileUsage() const { return connected() ? m_state->profileUsage() : QVariantMap(); }
QVariantList ProfileService::favoriteProfiles() const { return connected() ? m_state->favoriteProfiles() : QVariantList(); }
QString ProfileService::favoriteProfileOrder() const { return connected() ? m_state->favoriteProfileOrder() : QStringLiteral("usage"); }
QString ProfileService::autoLoadProfileFilename() const { return connected() ? m_state->autoLoadProfileFilename() : QString(); }
int ProfileService::autoLoadRevertMinutes() const { return connected() ? m_state->autoLoadRevertMinutes() : 0; }
QVariantMap ProfileService::currentProfileSummary() const { return connected() ? m_state->currentProfileSummary() : QVariantMap(); }
QVariantMap ProfileService::profileSummaries() const { return connected() ? m_state->profileSummaries() : QVariantMap(); }
QVariantMap ProfileService::roastAffinities() const { return connected() ? m_state->roastAffinities() : QVariantMap(); }
QVariantMap ProfileService::grindDirections() const { return connected() ? m_state->grindDirections() : QVariantMap(); }
QVariantMap ProfileService::flowCalibration() const { return connected() ? m_state->flowCalibration() : QVariantMap(); }

void ProfileService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed)) return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending();
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<ProfileStateReplica>() : new ProfileStateReplica;
    m_state->setParent(this);
    if (node) m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &ProfileService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::readyChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::loadingChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::errorMessageChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::catalogRevisionChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::allProfilesListChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::availableProfilesChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::installedProfileTitlesChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::baseProfileNameChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::previousProfileNameChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::profileModifiedChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::currentProfileReadOnlyChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::profileUsageChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::favoriteProfilesChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::favoriteProfileOrderChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::autoLoadProfileFilenameChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::autoLoadRevertMinutesChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::profileSummariesChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::currentProfileSummaryChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::roastAffinitiesChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::grindDirectionsChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::flowCalibrationChanged, this, &ProfileService::changed);
    connect(m_state, &ProfileStateReplica::profileDetailResult, this,
        [this](const QString& id, const QString& filename, const QVariantMap& detail,
               const QString& reason) {
            if (!m_detailRequests.contains(id) || m_detailRequests.take(id) != filename) return;
            if (reason.isEmpty() && !detail.isEmpty()) m_detailCache.insert(filename, detail);
            emit profileDetailReady(id, filename, detail, reason);
        }, Qt::QueuedConnection);
    connect(m_state, &ProfileStateReplica::knowledgeResult, this,
        [this](const QString& id, const QString& title, const QString& json,
               const QVariantMap& payload, const QString& reason) {
            auto request = m_knowledgeRequests.find(id);
            if (request == m_knowledgeRequests.end()) return;
            const auto expected = request.value();
            if (reason.isEmpty() && (expected.first != title || expected.second != json)) return;
            m_knowledgeRequests.erase(request);
            emit knowledgeReady(id, expected.first, expected.second, payload, reason);
        }, Qt::QueuedConnection);
    connect(m_state, &ProfileStateReplica::operationCompleted, this,
        [this](const QString& id, const QString& op, const QString& filename, bool success,
               const QString& reason, const QVariantMap& result) {
            m_operation.finish(id, success, -1, reason, result);
            Q_UNUSED(op)
            Q_UNUSED(filename)
        }, Qt::QueuedConnection);
    changed();
}

void ProfileService::changed()
{
    if (!connected()) failPending();
    const qint64 revision = catalogRevision();
    if (revision != m_cachedRevision) {
        m_cachedRevision = revision;
        m_detailCache.clear();
    }
    emit stateChanged();
}

void ProfileService::failPending()
{
    m_operation.disconnectPending();
    const auto details = std::exchange(m_detailRequests, {});
    for (auto it = details.cbegin(); it != details.cend(); ++it) {
        const QString id = it.key();
        const QString filename = it.value();
        QMetaObject::invokeMethod(this, [this, id, filename] {
            emit profileDetailReady(id, filename, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
    }
    const auto knowledge = std::exchange(m_knowledgeRequests, {});
    for (auto it = knowledge.cbegin(); it != knowledge.cend(); ++it) {
        const QString id = it.key();
        const QString title = it.value().first;
        const QString json = it.value().second;
        QMetaObject::invokeMethod(this, [this, id, title, json] {
            emit knowledgeReady(id, title, json, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
    }
}

QString ProfileService::newRequestId() const { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

void ProfileService::refresh()
{
    if (connected()) { m_operation.clearError(); m_state->refresh(); }
    else emit stateChanged();
}

QString ProfileService::requestProfileDetail(const QString& filename)
{
    const QString id = newRequestId();
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id, filename] {
            emit profileDetailReady(id, filename, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    m_detailRequests.insert(id, filename);
    m_state->requestProfileDetail(id, filename);
    return id;
}

QString ProfileService::requestKnowledge(const QString& title, const QString& profileJson)
{
    const QString id = newRequestId();
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id, title, profileJson] {
            emit knowledgeReady(id, title, profileJson, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    m_knowledgeRequests.insert(id, {title, profileJson});
    m_state->requestKnowledge(id, title, profileJson);
    return id;
}

QVariantMap ProfileService::cachedProfileByFilename(const QString& filename) const
{ return m_detailCache.value(filename); }

QString ProfileService::findProfileByTitle(const QString& title) const
{
    for (const QVariant& value : allProfilesList()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("title")).toString() == title)
            return row.value(QStringLiteral("name")).toString();
    }
    return {};
}

QVariantMap ProfileService::profileCatalogInfoForTitle(const QString& title) const
{
    QVariantMap result;
    for (const QVariant& value : allProfilesList()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("title")).toString() != title) continue;
        result.insert(QStringLiteral("filename"), row.value(QStringLiteral("name")));
        result.insert(QStringLiteral("title"), row.value(QStringLiteral("title")));
        result.insert(QStringLiteral("beverageType"), row.value(QStringLiteral("beverageType")));
        result.insert(QStringLiteral("hasKnowledgeBase"), row.value(QStringLiteral("hasKnowledgeBase")));
        result.insert(QStringLiteral("kbDerivedFrom"), row.value(QStringLiteral("kbDerivedFrom")));
        result.insert(QStringLiteral("espressoTemperatureC"), row.value(QStringLiteral("espressoTemperature")));
        result.insert(QStringLiteral("targetWeightG"), row.value(QStringLiteral("targetWeight")));
        for (const QVariant& available : availableProfiles()) {
            const QVariantMap candidate = available.toMap();
            if (candidate.value(QStringLiteral("name")) == row.value(QStringLiteral("name"))) {
                result.insert(QStringLiteral("editorType"), candidate.value(QStringLiteral("editorType")));
                break;
            }
        }
        return result;
    }
    return {};
}

QString ProfileService::beverageBucketForTitle(const QString& title) const
{
    const QVariantMap info = profileCatalogInfoForTitle(title);
    return info.isEmpty() ? QString()
                          : BeverageType::bucket(info.value(QStringLiteral("beverageType")).toString());
}

QVariantMap ProfileService::profileSummaryForTitle(const QString& title) const
{ return profileSummaries().value(title).toMap(); }

bool ProfileService::profileHasKnowledge(const QString& title) const
{ return profileCatalogInfoForTitle(title).value(QStringLiteral("hasKnowledgeBase")).toBool(); }

QVariantList ProfileService::filterProfiles(const QVariantMap& chips, const QString& search,
                                           const QStringList& allowed) const
{
    QSet<QString> favorites;
    for (const QVariant& fav : favoriteProfiles())
        favorites.insert(fav.toMap().value(QStringLiteral("filename")).toString());
    return ProfileCatalogLogic::filter(allProfilesList(), chips, search, allowed, favorites);
}

QVariantMap ProfileService::facetCounts(const QVariantMap& chips, const QString& search,
                                        const QStringList& allowed) const
{
    QSet<QString> favorites;
    for (const QVariant& fav : favoriteProfiles())
        favorites.insert(fav.toMap().value(QStringLiteral("filename")).toString());
    return ProfileCatalogLogic::facetCounts(allProfilesList(), chips, search, allowed, favorites);
}

QVariantList ProfileService::sortedByRecentUse(const QVariantList& entries,
                                              const QString& pinnedFilename) const
{ return ProfileCatalogLogic::sortedByRecentUse(entries, pinnedFilename, profileUsage()); }

bool ProfileService::teaProfileMatchesType(const QString& title, const QString& teaType) const
{ return DrinkTypes::teaProfileMatchesType(title, teaType); }
double ProfileService::defaultTeaTempC(const QString& teaType) const
{ return DrinkTypes::defaultTeaTempC(teaType); }

bool ProfileService::kbProfileSuitsRoast(const QString& title, const QString& roastLevel) const
{
    QString normalized = roastLevel.trimmed().toLower();
    normalized.replace(QLatin1Char(' '), QLatin1Char('-'));
    return roastAffinities().value(title).toStringList().contains(normalized);
}

QString ProfileService::grindDirectionBetween(const QString& sourceTitle,
                                              const QString& targetTitle) const
{ return grindDirections().value(sourceTitle).toMap().value(targetTitle).toString(); }

bool ProfileService::isFavoriteProfile(const QString& filename) const
{
    for (const QVariant& fav : favoriteProfiles())
        if (fav.toMap().value(QStringLiteral("filename")).toString() == filename) return true;
    return false;
}

QString ProfileService::execute(const QString& operation, const QString& filename,
                                const QVariantMap& fields)
{ return send(operation, filename, fields); }
QString ProfileService::loadProfile(const QString& filename) { return send(QStringLiteral("load"), filename); }
QString ProfileService::deleteProfile(const QString& filename) { return send(QStringLiteral("delete"), filename); }
QString ProfileService::duplicateProfile(const QString& filename, const QString& title)
{ return send(QStringLiteral("duplicate"), filename, {{QStringLiteral("title"), title}}); }
QString ProfileService::renameProfile(const QString& filename, const QString& title)
{ return send(QStringLiteral("rename"), filename, {{QStringLiteral("title"), title}}); }
QString ProfileService::toggleFavoriteProfile(const QString& filename)
{ return send(QStringLiteral("toggleFavorite"), filename); }
QString ProfileService::saveFavoritesOrder(const QStringList& filenames, const QString& mode)
{
    QVariantList values;
    for (const QString& filename : filenames) values.append(filename);
    return send(QStringLiteral("setFavoritesOrder"), {},
                {{QStringLiteral("filenames"), values}, {QStringLiteral("mode"), mode}});
}
QString ProfileService::setAutoLoadProfileFilename(const QString& filename)
{ return send(QStringLiteral("setAutoLoad"), {}, {{QStringLiteral("filename"), filename}}); }
QString ProfileService::setAutoLoadRevertMinutes(int minutes)
{ return send(QStringLiteral("setAutoLoadRevertMinutes"), {}, {{QStringLiteral("minutes"), minutes}}); }

QString ProfileService::send(const QString& operation, const QString& filename,
                             const QVariantMap& fields)
{
    const QString uiId = newRequestId();
    const qint64 expectedRevision = catalogRevision();
    m_operationFiles.insert(uiId, filename);
    const QString wireId = m_operation.begin(operation, -1, uiId, ready());
    if (!wireId.isEmpty()) m_state->execute(wireId, operation, filename, fields, expectedRevision);
    return uiId;
}

QString ProfileService::saveDraft(const QString& filename, const QVariantMap& editedMap,
                                  const QString& expectedSourceRevision, const QString& saveAsTitle,
                                  const QString& overwriteRevision)
{
    return send(QStringLiteral("saveDraft"), filename,
                {{QStringLiteral("draft"), editedMap},
                 {QStringLiteral("sourceRevision"), expectedSourceRevision},
                 {QStringLiteral("saveAsTitle"), saveAsTitle},
                 {QStringLiteral("overwriteRevision"), overwriteRevision}});
}

QString ProfileService::tryDraft(const QString& filename, const QVariantMap& editedMap,
                                 const QString& expectedSourceRevision)
{
    return send(QStringLiteral("tryDraft"), filename,
                {{QStringLiteral("draft"), editedMap},
                 {QStringLiteral("sourceRevision"), expectedSourceRevision}});
}
