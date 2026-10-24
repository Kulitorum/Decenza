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
    const bool wasReady = m_wasConnected || connected();
    const QPointer<BeanSearchStateReplica> previous = m_state;
    const bool cancelPrevious = connected();
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this, previous, cancelPrevious] {
                if (previous && cancelPrevious && !m_requestId.isEmpty())
                    previous->cancelSearch(m_requestId);
                failPending();
            }))
        return;
    const QPointer<BeanSearchService> bindingGuard(this);
    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &BeanSearchService::updateConnectionState);
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &BeanSearchService::updateConnectionState);
    connect(m_state, &BeanSearchStateReplica::resultsReady, this,
        [this](const QString& requestId, const QVariantList& rows, bool searching,
               bool complete, const QString& error) {
            if (requestId != m_requestId)
                return;
            const QPointer<BeanSearchService> guard(this);
            m_results = rows;
            const bool searchingChanged = std::exchange(m_searching, searching) != searching;
            const bool errorChanged = m_errorMessage != error;
            m_errorMessage = error;
            if (complete)
                m_requestId.clear();
            emit resultsChanged();
            if (!guard) return;
            if (searchingChanged) emit this->searchingChanged();
            if (!guard) return;
            if (errorChanged) emit errorMessageChanged();
        }, Qt::QueuedConnection);
    m_wasConnected = connected();
    if (wasReady != m_wasConnected)
        emit readyChanged();
    if (!bindingGuard) return;
    if (m_wasConnected && m_searchRequested)
        startSearch();
    if (!bindingGuard) return;
    emit resultsChanged();
}

void BeanSearchService::setQuery(const QString& query)
{
    const QString value = query.trimmed();
    if (m_query == value)
        return;
    m_query = value;
    const QPointer<BeanSearchService> guard(this);
    emit queryChanged();
    if (!guard) return;
    m_results.clear();
    emit resultsChanged();
    if (!guard) return;
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
    const QPointer<BeanSearchService> guard(this);
    emit bagKindChanged();
    if (!guard) return;
    m_results.clear();
    emit resultsChanged();
    if (!guard) return;
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
    const QPointer<BeanSearchService> guard(this);
    emit readyChanged();
    if (!guard) return;
    if (nowConnected) {
        if (m_searchRequested)
            startSearch();
        return;
    }
    failPending();
    if (!guard) return;
    if (m_searchRequested)
        setErrorMessage(QStringLiteral("Bean search service is disconnected."));
}

void BeanSearchService::startSearch()
{
    const QPointer<BeanSearchService> guard(this);
    if (!connected()) {
        failPending();
        if (!guard) return;
        setErrorMessage(QStringLiteral("Bean search service is disconnected."));
        return;
    }
    const QString requestId = newRequestId();
    const QString canceledId = std::exchange(m_requestId, requestId);
    const QPointer<BeanSearchStateReplica> requested = m_state;
    const QPointer<QRemoteObjectNode> requestedNode = m_node;
    setSearching(true);
    if (!guard) return;
    setErrorMessage(QString());
    if (!guard) return;
    if (requested != m_state || m_requestId != requestId || !connected()) {
        if (!canceledId.isEmpty()
            && RemoteServiceReplica::isReady(requestedNode, requested, s_publishedNode))
            requested->cancelSearch(canceledId);
        return;
    }
    requested->search(requestId, canceledId, m_query, m_bagKind);
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
    const bool wasSearching = std::exchange(m_searching, false);
    const QPointer<BeanSearchService> guard(this);
    emit resultsChanged();
    if (!guard) return;
    if (wasSearching)
        emit searchingChanged();
}
