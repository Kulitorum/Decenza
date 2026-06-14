#include "beanbaseclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
// Visualizer's official canonical search API (open-source: miharekar/visualizer,
// Api::CanonicalCoffeeBagsController — unauthenticated, substring + multi-word
// matching, documented in openapi.yaml). The search response carries the
// canonical UUID we store locally AND send on shot PATCH, plus the full
// descriptive block, so one request is enough.
constexpr auto kVisualizerBaseUrl = "https://visualizer.coffee";

// Canonical search: type-ahead cadence. The endpoint is rate-limited
// (50 req/min per IP); debounce + session cache keep a single user well under it.
constexpr int kCanonicalDebounceMs = 350;

// Stalled-connection guard on every request (matches shotserver/aiprovider).
// Surfaces as OperationCanceledError; handlers distinguish our own
// supersede-abort (reply already detached) from a timeout (still active).
constexpr int kTransferTimeoutMs = 15000;

// Visualizer API column -> our blob key (the vocabulary downstream consumers —
// popup, advisor block, uploads — expect). Empty/null values are dropped by the
// caller. elevation is a single display string here.
constexpr struct { const char* apiKey; const char* blobKey; } kAttrMap[] = {
    {"roast_level", "degree"},
    {"country", "origin"},
    {"region", "region"},
    {"farmer", "producer"},
    {"variety", "variety"},
    {"processing", "process"},
    {"harvest_time", "harvest"},
    {"tasting_notes", "tastingNotes"},
    {"elevation", "elevation"},
};
}  // namespace

BeanBaseClient::BeanBaseClient(QNetworkAccessManager* networkManager,
                               Settings* settings, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_visualizerBaseUrl(QString::fromLatin1(kVisualizerBaseUrl))
{
    // settings retained in the signature for call-site stability; the canonical
    // (Visualizer) path is keyless, so no Settings access is needed here.
    Q_UNUSED(settings);

    m_canonicalDebounceTimer.setSingleShot(true);
    m_canonicalDebounceTimer.setInterval(kCanonicalDebounceMs);
    connect(&m_canonicalDebounceTimer, &QTimer::timeout, this, [this]() {
        // Copy before clearing: doSendCanonicalSearch takes a const ref, and
        // passing the member directly would alias the string we clear.
        const QString q = m_pendingCanonicalQuery;
        m_pendingCanonicalQuery.clear();
        if (!q.isEmpty())
            doSendCanonicalSearch(q);
    });
}

void BeanBaseClient::search(const QString& query) {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;

    const QString normalized = trimmed.toLower();
    const auto cached = m_canonicalCache.constFind(normalized);
    if (cached != m_canonicalCache.constEnd()) {
        emit searchResults(trimmed, cached.value());
        return;
    }

    // Latest-wins on a SHARED client (Beans page + MCP): tell the displaced
    // query's consumer it will never get an answer, so nothing waits forever
    // (MCP gather, search-bar spinner).
    if (!m_pendingCanonicalQuery.isEmpty()
        && m_pendingCanonicalQuery.compare(trimmed, Qt::CaseInsensitive) != 0)
        emit searchFailed(m_pendingCanonicalQuery, QStringLiteral("superseded"));
    m_pendingCanonicalQuery = trimmed;
    m_canonicalDebounceTimer.start();
}

void BeanBaseClient::doSendCanonicalSearch(const QString& query) {
    if (!m_networkManager) {
        emit searchFailed(query, QStringLiteral("network"));
        return;
    }
    if (m_activeCanonicalReply) {
        // Supersede the in-flight request. abort() emits finished() SYNCHRONOUSLY
        // (same-thread direct connection), so detach the pointer FIRST: the
        // handler then sees wasActive == false and stays silent, leaving this the
        // single terminal signal ("superseded") for the displaced query. Aborting
        // before clearing would let the handler also emit a spurious "network".
        QNetworkReply* superseded = m_activeCanonicalReply;
        m_activeCanonicalReply.clear();
        emit searchFailed(m_activeCanonicalQuery, QStringLiteral("superseded"));
        superseded->abort();
    }

    QUrl url(QStringLiteral("%1/api/canonical_coffee_bags").arg(m_visualizerBaseUrl));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(urlQuery);

    QNetworkRequest request{url};
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = m_networkManager->get(request);
    m_activeCanonicalReply = reply;
    m_activeCanonicalQuery = query;
    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        reply->deleteLater();
        const bool wasActive = (m_activeCanonicalReply == reply);
        if (wasActive)
            m_activeCanonicalReply.clear();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            // Our own supersede-abort already emitted; a transfer TIMEOUT
            // (reply still active) has not — report it.
            if (wasActive)
                emit searchFailed(query, QStringLiteral("network"));
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            bool parsedOk = false;
            const QVariantList entries = parseCanonicalCoffeeBags(reply->readAll(), &parsedOk);
            if (!parsedOk) {
                // 200 with a non-JSON body — the API contract drifted, not a
                // missing bean. Surface as parse so the bar's copy stays honest.
                emit searchFailed(query, QStringLiteral("parse"));
                return;
            }
            m_canonicalCache.insert(query.toLower(), entries);
            emit searchResults(query, entries);
        } else {
            // Non-200 (incl. HTTP 429 rate limit) — a reach failure, not an empty
            // result; the bar must not render "No matches".
            emit searchFailed(query, QStringLiteral("network"));
        }
    });
}

void BeanBaseClient::fetchCanonicalDetails(const QVariantMap& entry) {
    const QString canonicalId = entry.value(QStringLiteral("id")).toString();
    if (canonicalId.isEmpty())
        return;

    // The /api search already carried every descriptive field on the entry, so
    // enrichment is a local re-emit with no network round-trip. Build the attrs
    // map from the descriptive blob keys; canonicalRoasterId rides along only
    // when there is descriptive data (matches the prior payload-gated emit).
    QVariantMap attrs;
    for (const char* blobKey : {"degree", "origin", "region", "producer", "variety",
                                "process", "harvest", "tastingNotes", "elevation"}) {
        const QString v = entry.value(QLatin1String(blobKey)).toString();
        if (!v.isEmpty())
            attrs.insert(QLatin1String(blobKey), v);
    }
    if (attrs.isEmpty())
        return;  // No descriptive data — nothing to enrich (the gather's grace
                 // timer covers it, as the old payload-less path did).

    const QString roasterId = entry.value(QStringLiteral("canonicalRoasterId")).toString();
    if (!roasterId.isEmpty())
        attrs.insert(QStringLiteral("canonicalRoasterId"), roasterId);

    // Deferred so the emit stays asynchronous: consumers (the MCP gather, QML
    // Connections) connect canonicalDetails before — or right after — invoking
    // this, and a synchronous emit could fire before a just-after connect.
    QPointer<BeanBaseClient> self(this);
    QMetaObject::invokeMethod(this, [self, canonicalId, attrs]() {
        if (self)
            emit self->canonicalDetails(canonicalId, attrs);
    }, Qt::QueuedConnection);
}

QVariantList BeanBaseClient::parseCanonicalCoffeeBags(const QByteArray& json, bool* parsedOk) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    const bool ok = (parseError.error == QJsonParseError::NoError && doc.isObject());
    if (parsedOk)
        *parsedOk = ok;
    if (!ok)
        return {};

    QVariantList out;
    const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue& value : data) {
        const QJsonObject bag = value.toObject();
        const QString id = bag.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        QVariantMap entry;
        entry[QStringLiteral("id")] = id;
        entry[QStringLiteral("visualizerCanonicalId")] = id;
        entry[QStringLiteral("source")] = QStringLiteral("visualizer");

        const QString roaster = bag.value(QStringLiteral("canonical_roaster_name")).toString();
        if (!roaster.isEmpty())
            entry[QStringLiteral("roasterName")] = roaster;
        const QString name = bag.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            entry[QStringLiteral("roastName")] = name;
        const QString roasterId = bag.value(QStringLiteral("canonical_roaster_id")).toString();
        if (!roasterId.isEmpty())
            entry[QStringLiteral("canonicalRoasterId")] = roasterId;

        for (const auto& m : kAttrMap) {
            const QString v = bag.value(QLatin1String(m.apiKey)).toString();  // null -> ""
            if (!v.isEmpty())
                entry[QLatin1String(m.blobKey)] = v;
        }
        out.append(entry);
    }
    return out;
}
