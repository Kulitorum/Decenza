#include "libraryservice.h"

#include "core/widgetlibrary.h"
#include "network/communitythumbnail.h"
#include "servicepayload.h"
#include "serviceoperation.h"
#include <QRemoteObjectNode>
#include <QFileInfo>
#include <QQmlEngine>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
QPointer<WidgetLibrary> s_publishedLocalLibrary;

#ifdef Q_OS_WASM
QVariantMap sameOriginCommunityThumbnails(QVariantMap result, const QUrl& origin)
{
    QVariantList entries = result.value(QStringLiteral("entries")).toList();
    for (QVariant& value : entries) {
        QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString full = entry.value(QStringLiteral("thumbnailFullUrl")).toString();
        const QString compact = entry.value(QStringLiteral("thumbnailCompactUrl")).toString();
        if (CommunityThumbnail::hasCanonicalThumbnail(id, full, false)) {
            entry.insert(QStringLiteral("thumbnailFullUrl"),
                         CommunityThumbnail::resolvedUrl(origin, id, false));
        } else {
            entry.remove(QStringLiteral("thumbnailFullUrl"));
        }
        if (CommunityThumbnail::hasCanonicalThumbnail(id, compact, true)) {
            entry.insert(QStringLiteral("thumbnailCompactUrl"),
                         CommunityThumbnail::resolvedUrl(origin, id, true));
        } else {
            entry.remove(QStringLiteral("thumbnailCompactUrl"));
        }
        value = entry;
    }
    result.insert(QStringLiteral("entries"), entries);
    return result;
}
#endif
}

LibraryService::LibraryService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

LibraryService::~LibraryService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}

void LibraryService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
void LibraryService::setPublishedLocalLibrary(WidgetLibrary* library) { s_publishedLocalLibrary = library; }
QRemoteObjectNode* LibraryService::publishedNode() { return s_publishedNode; }

LibraryService* LibraryService::create(QQmlEngine* engine)
{
    return new LibraryService(publishedNode(), engine);
}

bool LibraryService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

QVariantList LibraryService::localEntries() const
{
    return connected() ? m_state->localEntries() : QVariantList();
}

quint64 LibraryService::revision() const
{
    return connected() ? m_state->revision() : 0;
}

bool LibraryService::ready() const
{
    return connected() && m_state->ready();
}

QString LibraryService::errorMessage() const
{
    if (!connected()) return tr("Library service is disconnected.");
    return m_state->errorMessage();
}

void LibraryService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(QStringLiteral("disconnected")); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &LibraryService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &LibraryService::changed);
    connect(m_state, &LibraryStateReplica::localEntriesChanged,
            this, &LibraryService::changed);
    connect(m_state, &LibraryStateReplica::revisionChanged,
            this, &LibraryService::changed);
    connect(m_state, &LibraryStateReplica::readyChanged,
            this, &LibraryService::changed);
    connect(m_state, &LibraryStateReplica::errorMessageChanged,
            this, &LibraryService::changed);
    connect(m_state, &LibraryStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        if (m_pending.value(id) != operation) return;
        m_pending.remove(id);
        emit busyChanged();
        QVariantMap projected = result;
#ifdef Q_OS_WASM
        if (success && (operation == QLatin1String("browseCommunity")
                        || operation == QLatin1String("browseMyUploads")
                        || operation == QLatin1String("featured")))
            projected = sameOriginCommunityThumbnails(std::move(projected), resourceOrigin());
#endif
        emit operationCompleted(id, operation, success, projected, reason);
    }, Qt::QueuedConnection);
    changed();
}

void LibraryService::changed()
{
    if (!connected()) failPending(QStringLiteral("disconnected"));
    emit stateChanged();
}

void LibraryService::failPending(const QString& reason)
{
    if (m_pending.isEmpty()) return;
    const auto pending = m_pending;
    m_pending.clear();
    emit busyChanged();
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        emit operationCompleted(it.key(), it.value(), false, {}, reason);
}

QString LibraryService::newRequestId() const
{
    return ServiceOperation::newId();
}

void LibraryService::refresh()
{
    if (connected()) m_state->refresh();
}

QString LibraryService::execute(const QString& operation, const QVariantMap& arguments,
                                const QString& requestId)
{
    const QString id = requestId.isEmpty() ? newRequestId() : requestId;
    if (id.isEmpty() || m_pending.contains(id)) return QString();
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return id;
    }
    if (m_pending.size() >= 64) {
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("tooManyRequests"));
        }, Qt::QueuedConnection);
        return id;
    }
    const bool wasBusy = busy();
    m_pending.insert(id, operation);
    if (!wasBusy) emit busyChanged();
    m_state->execute(id, operation, arguments);
    return id;
}

QString LibraryService::thumbnailUrl(const QString& entryId, bool compact) const
{
    if (entryId.isEmpty()) return {};
#ifdef Q_OS_WASM
    QUrl url(QStringLiteral("/api/library/thumbnail"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), entryId);
    if (compact) query.addQueryItem(QStringLiteral("compact"), QStringLiteral("1"));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
#else
    if (!s_publishedLocalLibrary) return {};
    const QString path = compact && s_publishedLocalLibrary->hasThumbnailCompact(entryId)
        ? s_publishedLocalLibrary->thumbnailCompactPath(entryId)
        : s_publishedLocalLibrary->thumbnailPath(entryId);
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString();
#endif
}
