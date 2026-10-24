#include "beansearchservicesource.h"

#include "core/dbutils.h"
#include "core/settings.h"
#include "history/coffeebagstorage.h"
#include "history/unifiedbeansearchmodel.h"
#include "network/beanbaseclient.h"
#include "servicepayload.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QRunnable>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <utility>

namespace {
constexpr int kMaxConcurrentSearches = 12;
constexpr qsizetype kMaxQueryLength = 256;
// Leave room for the QtRO signal name, request id, status and error fields.
constexpr qint64 kMaxResultsPayloadBytes = 1536 * 1024;

bool resultsPayloadFits(const QVariantList& results)
{
    return ServicePayload::fits(results, kMaxResultsPayloadBytes);
}
}

class BeanSearchQuery final : public QObject
{
public:
    BeanSearchQuery(QString requestToken, QString searchText, QString bagKind,
                    Settings* settings, QString dbPath, QObject* parent)
        : QObject(parent), requestId(std::move(requestToken)), query(std::move(searchText)),
          kind(std::move(bagKind)), databasePath(std::move(dbPath)),
          network(new QNetworkAccessManager(this)),
          beanBase(new BeanBaseClient(network, settings, this))
    {
    }

    QString requestId;
    QString query;
    QString kind;
    QString databasePath;
    QPointer<QNetworkAccessManager> network;
    QPointer<BeanBaseClient> beanBase;
    QVariantList inventory;
    QVariantList history;
    QVariantList canonical;
    QSet<QString> pendingLinks;
    QSet<QString> attemptedLinks;
    bool inventoryDone = false;
    bool historyDone = false;
    bool canonicalDone = false;
    QString laneError;
};

NativeBeanSearchServiceSource::NativeBeanSearchServiceSource(CoffeeBagStorage* bagStorage,
                                                             Settings* settings,
                                                             QString databasePath,
                                                             QObject* parent)
    : BeanSearchStateSimpleSource(parent), m_bagStorage(bagStorage), m_settings(settings),
      m_databasePath(std::move(databasePath))
{
}

NativeBeanSearchServiceSource::~NativeBeanSearchServiceSource()
{
    m_destroyed->store(true, std::memory_order_release);
}

void NativeBeanSearchServiceSource::search(QString requestId, QString cancelRequestId,
                                           QString query, QString bagKind)
{
    const auto start = m_requests.begin(requestId);
    if (start != ServiceRequestIds::Start::Accepted) {
        if (start != ServiceRequestIds::Start::DuplicateId) {
            const QString reason = start == ServiceRequestIds::Start::LimitReached
                ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
            QMetaObject::invokeMethod(this, [this, requestId, reason] {
                emit resultsReady(requestId, {}, false, true, reason);
            }, Qt::QueuedConnection);
        }
        return;
    }

    // Reserve the new request before cancelling its predecessor; malformed
    // requests must not be able to tear down a live search.
    cancel(cancelRequestId);
    if (query.size() > kMaxQueryLength || (bagKind != QLatin1String("")
                                            && bagKind != QLatin1String("tea"))) {
        m_requests.finish(requestId);
        emit resultsReady(requestId, {}, false, true, QStringLiteral("invalidRequest"));
        return;
    }
    if (m_queries.size() >= kMaxConcurrentSearches) {
        m_requests.finish(requestId);
        emit resultsReady(requestId, {}, false, true, QStringLiteral("tooManyRequests"));
        return;
    }
    if (!m_databasePath.isEmpty() && m_activeHistoryReads >= kMaxConcurrentSearches) {
        m_requests.finish(requestId);
        emit resultsReady(requestId, {}, false, true, QStringLiteral("historyBusy"));
        return;
    }

    query = query.trimmed();
    auto* job = new BeanSearchQuery(requestId, query, bagKind, m_settings, m_databasePath, this);
    m_queries.insert(requestId, job);
    connect(job->beanBase, &BeanBaseClient::searchResults, job,
        [this, job](const QString& returnedQuery, const QVariantList& rows) {
            if (returnedQuery.compare(job->query, Qt::CaseInsensitive) != 0)
                return;
            job->canonical = rows;
            job->canonicalDone = true;
            publish(job);
        });
    connect(job->beanBase, &BeanBaseClient::searchFailed, job,
        [this, job](const QString& returnedQuery, const QString& reason) {
            if (returnedQuery.compare(job->query, Qt::CaseInsensitive) != 0)
                return;
            job->canonical.clear();
            job->canonicalDone = true;
            if (reason != QLatin1String("superseded"))
                job->laneError = reason == QLatin1String("responseTooLarge")
                    ? QStringLiteral("canonicalResponseTooLarge")
                    : QStringLiteral("canonicalSearchFailed");
            publish(job, job->laneError);
        });
    connect(job->beanBase, &BeanBaseClient::linkStateResolved, job,
        [this, job](const QString& url, const QString&) {
            if (!job->pendingLinks.remove(url))
                return;
            publish(job, job->laneError);
        });

    if (!m_bagStorage) {
        job->inventoryDone = true;
        job->laneError = QStringLiteral("inventoryUnavailable");
    } else {
        m_bagStorage->requestInventoryWithCompletion(job,
            [this, job](bool success, const QVariantList& rows, const QString& reason) {
                if (!m_queries.contains(job->requestId))
                    return;
                job->inventoryDone = true;
                if (success)
                    job->inventory = UnifiedBeanSearchModel::inventoryForKind(rows, job->kind);
                else
                    job->laneError = reason.isEmpty() ? QStringLiteral("inventoryUnavailable") : reason;
                publish(job, job->laneError);
            });
    }

    if (m_databasePath.isEmpty()) {
        job->historyDone = true;
        if (job->laneError.isEmpty()) job->laneError = QStringLiteral("historyUnavailable");
    } else {
        startHistoryRead(job);
    }

    if (query.isEmpty() || bagKind == QLatin1String("tea")) {
        job->canonicalDone = true;
    } else {
        job->beanBase->search(query);
    }
    publish(job, job->laneError);
}

void NativeBeanSearchServiceSource::startHistoryRead(BeanSearchQuery* job)
{
    auto rows = std::make_shared<QVariantList>();
    auto opened = std::make_shared<bool>(false);
    auto destroyed = m_destroyed;
    const QString path = job->databasePath;
    const QString query = job->query;
    const QString kind = job->kind;
    QPointer<BeanSearchQuery> guardedJob(job);
    auto* completionContext = QCoreApplication::instance();
    ++m_activeHistoryReads;
    // QCoreApplication joins this pool before destroying its QObject context
    // (Qt 6.12 qcoreapplication.cpp:957-968).
    QThreadPool::globalInstance()->start(QRunnable::create(
        [this, path, query, kind, rows, opened, destroyed, guardedJob, completionContext] {
        *opened = withTempDb(path, "bean_qtro_search", [&](QSqlDatabase& db) {
            *rows = UnifiedBeanSearchModel::queryHistoryStatic(db, query, 50, kind);
        });
        // Query cancellation and service destruction both happen on the main thread.
        // Inspect their lifetime only after returning there.
        QMetaObject::invokeMethod(completionContext, [this, guardedJob, rows, opened, destroyed] {
            if (destroyed->load(std::memory_order_acquire))
                return;
            --m_activeHistoryReads;
            if (!guardedJob || m_queries.value(guardedJob->requestId) != guardedJob)
                return;
            guardedJob->historyDone = true;
            if (*opened)
                guardedJob->history = *rows;
            else if (guardedJob->laneError.isEmpty())
                guardedJob->laneError = QStringLiteral("historyUnavailable");
            publish(guardedJob, guardedJob->laneError);
        }, Qt::QueuedConnection);
    }));
}

void NativeBeanSearchServiceSource::publish(BeanSearchQuery* job, const QString& error)
{
    if (!job || m_queries.value(job->requestId) != job)
        return;
    if (!job->inventoryDone || !job->historyDone || !job->canonicalDone)
        return;

    const QVariantList merged = UnifiedBeanSearchModel::mergeLanes(
        job->inventory, job->canonical, job->history, job->query);
    if (!resultsPayloadFits(merged)) {
        const QString requestId = job->requestId;
        finish(job);
        emit resultsReady(requestId, {}, false, true, QStringLiteral("resultTooLarge"));
        return;
    }
    QHash<QString, QString> linkStates;
    QStringList newProbes;
    for (const QVariant& value : merged) {
        const QString link = UnifiedBeanSearchModel::rowLink(value.toMap());
        if (link.isEmpty())
            continue;
        const QString state = job->beanBase->linkState(link);
        linkStates.insert(link, state);
        const QUrl parsed(link);
        if (state == QLatin1String("unknown") && parsed.isValid()
            && (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https"))
            && !job->attemptedLinks.contains(link)) {
            // An unknown answer is terminal for this query too. Rebuilding
            // results must neither retry it nor restart other pending probes.
            job->attemptedLinks.insert(link);
            job->pendingLinks.insert(link);
            newProbes.append(link);
        }
    }
    const QVariantList ordered = UnifiedBeanSearchModel::orderByLinkState(merged, linkStates);
    const bool complete = job->pendingLinks.isEmpty();
    const QString requestId = job->requestId;
    const QString resultError = error;
    if (complete) {
        finish(job);
        emit resultsReady(requestId, ordered, false, true, resultError);
        return;
    }
    QPointer<NativeBeanSearchServiceSource> owner(this);
    QPointer<BeanSearchQuery> guardedJob(job);
    emit resultsReady(requestId, ordered, true, false, resultError);
    for (const QString& url : std::as_const(newProbes)) {
        // A result observer may cancel this query or destroy its source.
        if (!owner || !guardedJob || owner->m_queries.value(requestId) != guardedJob)
            return;
        guardedJob->beanBase->probeLinkState(url);
    }
}

void NativeBeanSearchServiceSource::cancelSearch(QString requestId)
{
    cancel(requestId);
}

void NativeBeanSearchServiceSource::cancel(const QString& requestId)
{
    auto it = m_queries.find(requestId);
    if (it == m_queries.end())
        return;
    BeanSearchQuery* job = it.value();
    m_queries.erase(it);
    m_requests.finish(requestId);
    job->deleteLater();
}

bool NativeBeanSearchServiceSource::finish(BeanSearchQuery* job)
{
    if (!job || m_queries.value(job->requestId) != job)
        return false;
    const QString id = job->requestId;
    m_queries.remove(id);
    m_requests.finish(id);
    job->deleteLater();
    return true;
}
