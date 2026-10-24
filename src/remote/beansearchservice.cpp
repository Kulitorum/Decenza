#include "beansearchservice.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
QString newRequestId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

BeanSearchService::BeanSearchService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

BeanSearchService::~BeanSearchService()
{
    if (m_state && connected() && !m_requestId.isEmpty())
        m_state->cancelSearch(m_requestId);
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void BeanSearchService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* BeanSearchService::publishedNode() { return s_publishedNode; }

bool BeanSearchService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

void BeanSearchService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;
    const bool wasReady = m_wasConnected || connected();
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending();
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<BeanSearchStateReplica>() : new BeanSearchStateReplica;
    m_state->setParent(this);
    if (node)
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });

    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &BeanSearchService::updateConnectionState);
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &BeanSearchService::updateConnectionState);
    connect(m_state, &BeanSearchStateReplica::resultsReady, this,
        [this](const QString& requestId, const QVariantList& rows, bool searching,
               bool complete, const QString& error) {
            if (requestId != m_requestId)
                return;
            m_results = rows;
            emit resultsChanged();
            setSearching(searching);
            setErrorMessage(error);
            if (complete)
                m_requestId.clear();
        }, Qt::QueuedConnection);
    m_wasConnected = connected();
    if (wasReady != m_wasConnected)
        emit readyChanged();
    if (m_wasConnected && m_searchRequested)
        startSearch();
    emit resultsChanged();
}

void BeanSearchService::setQuery(const QString& query)
{
    const QString value = query.trimmed();
    if (m_query == value)
        return;
    m_query = value;
    emit queryChanged();
    m_results.clear();
    emit resultsChanged();
    if (!value.isEmpty())
        m_searchRequested = true;
    if (m_searchRequested)
        startSearch();
}

void BeanSearchService::setBagKind(const QString& kind)
{
    const QString value = kind == QLatin1String("tea") ? kind : QString();
    if (m_bagKind == value)
        return;
    m_bagKind = value;
    emit bagKindChanged();
    m_results.clear();
    emit resultsChanged();
    // A mode change scopes a search already requested by the user. Merely
    // initializing this filter must not trigger an external canonical lookup.
    if (m_searchRequested)
        startSearch();
}

QVariantMap BeanSearchService::get(int row) const
{
    if (row < 0 || row >= m_results.size())
        return {};
    return m_results.at(row).toMap();
}

void BeanSearchService::refresh()
{
    m_searchRequested = true;
    startSearch();
}

void BeanSearchService::updateConnectionState()
{
    const bool nowConnected = connected();
    if (nowConnected == m_wasConnected)
        return;
    m_wasConnected = nowConnected;
    emit readyChanged();
    if (nowConnected) {
        if (m_searchRequested)
            startSearch();
        return;
    }
    failPending();
    if (m_searchRequested)
        setErrorMessage(QStringLiteral("Bean search service is disconnected."));
}

void BeanSearchService::startSearch()
{
    if (!connected()) {
        failPending();
        setErrorMessage(QStringLiteral("Bean search service is disconnected."));
        return;
    }
    const QString requestId = newRequestId();
    const QString canceledId = std::exchange(m_requestId, requestId);
    setSearching(true);
    setErrorMessage(QString());
    m_state->search(requestId, canceledId, m_query, m_bagKind);
}

void BeanSearchService::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void BeanSearchService::setErrorMessage(const QString& message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

void BeanSearchService::failPending()
{
    if (m_requestId.isEmpty())
        return;
    const QString requestId = std::exchange(m_requestId, {});
    if (m_state && connected())
        m_state->cancelSearch(requestId);
    m_results.clear();
    emit resultsChanged();
    setSearching(false);
}
