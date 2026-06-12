#include "beanbaseclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
// Visualizer's canonical autocomplete (open-source: miharekar/visualizer,
// CanonicalController — unauthenticated, substring + multi-word matching,
// returns the canonical UUID we store locally AND send on shot PATCH).
// Internal endpoint: parse defensively, degrade silently.
constexpr auto kVisualizerBaseUrl = "https://visualizer.coffee";

// Canonical search: type-ahead cadence; no documented limit on the endpoint,
// debounce + session cache keep usage polite.
constexpr int kCanonicalDebounceMs = 350;

// Stalled-connection guard on every request (matches shotserver/aiprovider).
// Surfaces as OperationCanceledError; handlers distinguish our own
// supersede-abort (reply already detached) from a timeout (still active).
constexpr int kTransferTimeoutMs = 15000;

// Minimal entity decode for attribute values in the autocomplete fragment
// (Rails-escaped: amp/lt/gt/quot/#39 cover what names/URLs contain).
QString htmlUnescape(QString s) {
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return s;
}

// Pulls attr="value" off an <li ...> tag chunk; empty when absent.
QString liAttr(const QString& li, const QString& attr) {
    const QString needle = attr + QStringLiteral("=\"");
    const qsizetype start = li.indexOf(needle);
    if (start < 0) return QString();
    const qsizetype valueStart = start + needle.size();
    const qsizetype end = li.indexOf(QLatin1Char('"'), valueStart);
    if (end < 0) return QString();
    return htmlUnescape(li.mid(valueStart, end - valueStart));
}

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
        // Aborting the in-flight request: its handler stays silent for the
        // abort itself, so emit the never-coming-answer signal here.
        emit searchFailed(m_activeCanonicalQuery, QStringLiteral("superseded"));
        m_activeCanonicalReply->abort();
        m_activeCanonicalReply.clear();
    }

    QUrl url(QStringLiteral("%1/canonical/autocomplete_coffee_bags").arg(m_visualizerBaseUrl));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(urlQuery);

    QNetworkRequest request{url};
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
            const QByteArray body = reply->readAll();
            const QVariantList entries = parseCanonicalAutocomplete(body);
            // Markup-drift tripwire: rows present but none parsed means OUR
            // attribute scraping broke, not that the bean is missing. Without
            // this, every user sees a plausible "No matches" forever.
            if (entries.isEmpty() && body.contains("<li")) {
                qWarning() << "BeanBaseClient: canonical fragment contained rows but none parsed"
                              " — markup drift? First 200 bytes:" << body.left(200);
                emit searchFailed(query, QStringLiteral("parse"));
                return;
            }
            m_canonicalCache.insert(query.toLower(), entries);
            emit searchResults(query, entries);
        } else {
            // 302/401 would mean the open endpoint got auth-gated — surface
            // as a generic reach failure; the bar's copy stays honest.
            emit searchFailed(query, QStringLiteral("network"));
        }
    });
}

void BeanBaseClient::fetchCanonicalDetails(const QVariantMap& entry) {
    const QString roasterName = entry.value(QStringLiteral("roasterName")).toString();
    const QString canonicalId = entry.value(QStringLiteral("id")).toString();
    if (roasterName.isEmpty() || canonicalId.isEmpty() || !m_networkManager)
        return;

    const auto cachedUuid = m_roasterUuidCache.constFind(roasterName.toLower());
    if (cachedUuid != m_roasterUuidCache.constEnd()) {
        fetchCanonicalPayload(cachedUuid.value(), entry);
        return;
    }

    QUrl url(QStringLiteral("%1/canonical/autocomplete_roasters").arg(m_visualizerBaseUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), roasterName);
    url.setQuery(q);

    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, entry, roasterName]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;  // Best-effort enrichment: transient failures stay silent.
        // Exact-name match (Visualizer itself keys canonical roasters by name).
        const QVariantList roasters = parseCanonicalAutocomplete(reply->readAll());
        for (const QVariant& v : roasters) {
            const QVariantMap r = v.toMap();
            if (r.value(QStringLiteral("roasterName")).toString()
                    .compare(roasterName, Qt::CaseInsensitive) == 0) {
                const QString uuid = r.value(QStringLiteral("id")).toString();
                m_roasterUuidCache.insert(roasterName.toLower(), uuid);
                fetchCanonicalPayload(uuid, entry);
                return;
            }
        }
        // No exact match. A single candidate IS the answer (the query was the
        // roaster's name); use it rather than failing enrichment for this
        // roaster forever. Anything else is a systematic mismatch worth a
        // log line — it's the difference between "best-effort" and
        // "broke months ago and nobody can tell".
        if (roasters.size() == 1) {
            const QString uuid = roasters.first().toMap().value(QStringLiteral("id")).toString();
            m_roasterUuidCache.insert(roasterName.toLower(), uuid);
            fetchCanonicalPayload(uuid, entry);
            return;
        }
        qWarning() << "BeanBaseClient: enrichment skipped — no exact roaster match for"
                   << roasterName << "(" << roasters.size() << "candidates)";
    });
}

void BeanBaseClient::fetchCanonicalPayload(const QString& roasterUuid, const QVariantMap& entry) {
    const QString canonicalId = entry.value(QStringLiteral("id")).toString();
    const QString roastName = entry.value(QStringLiteral("roastName")).toString();

    QUrl url(QStringLiteral("%1/canonical/autocomplete_coffee_bags").arg(m_visualizerBaseUrl));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), roastName);
    q.addQueryItem(QStringLiteral("require_roaster"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("canonical_roaster_id"), roasterUuid);
    url.setQuery(q);

    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, canonicalId, roasterUuid]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;  // Best-effort: transient failures stay silent.
        QVariantMap attrs = parseCanonicalPayload(reply->readAll(), canonicalId);
        if (!attrs.isEmpty()) {
            // Persist the roaster UUID in the blob — it otherwise lives only
            // in the in-memory m_roasterUuidCache and is lost on restart.
            // Visualizer Coffee Management bag creation needs it for verified
            // roaster linking (bean-bag-inventory).
            attrs.insert(QStringLiteral("canonicalRoasterId"), roasterUuid);
            emit canonicalDetails(canonicalId, attrs);
        } else {
            qWarning() << "BeanBaseClient: enrichment payload missing/unparseable for" << canonicalId;
        }
    });
}

QVariantList BeanBaseClient::parseCanonicalAutocomplete(const QByteArray& html) {
    QVariantList out;
    const QString doc = QString::fromUtf8(html);

    // Split on <li and read the role="option" rows' data attributes. The
    // fragment is tiny (a dropdown's worth of rows); attribute scanning via
    // liAttr keeps us independent of class names and whitespace.
    qsizetype pos = 0;
    while ((pos = doc.indexOf(QStringLiteral("<li"), pos)) != -1) {
        qsizetype end = doc.indexOf(QLatin1Char('>'), pos);
        if (end < 0) break;
        const QString li = doc.mid(pos, end - pos + 1);
        pos = end + 1;

        const QString uuid = liAttr(li, QStringLiteral("data-autocomplete-value"));
        if (uuid.isEmpty()) continue;

        QVariantMap entry;
        entry[QStringLiteral("id")] = uuid;
        entry[QStringLiteral("visualizerCanonicalId")] = uuid;
        entry[QStringLiteral("source")] = QStringLiteral("visualizer");
        entry[QStringLiteral("roasterName")] = liAttr(li, QStringLiteral("data-roaster"));
        // Bag rows carry data-coffee-bag; roaster rows carry data-roaster-website.
        const QString bag = liAttr(li, QStringLiteral("data-coffee-bag"));
        if (!bag.isEmpty())
            entry[QStringLiteral("roastName")] = bag;
        const QString website = liAttr(li, QStringLiteral("data-roaster-website"));
        if (!website.isEmpty())
            entry[QStringLiteral("roasterWebsite")] = website;
        out.append(entry);
    }
    return out;
}

QVariantMap BeanBaseClient::parseCanonicalPayload(const QByteArray& html, const QString& canonicalId) {
    const QString doc = QString::fromUtf8(html);

    // Find the <li> whose data-autocomplete-value matches, then the payload
    // div inside it (payload divs are nested within their <li>...</li>).
    const qsizetype liStart = doc.indexOf(
        QStringLiteral("data-autocomplete-value=\"") + canonicalId + QLatin1Char('"'));
    if (liStart < 0) return QVariantMap();
    qsizetype liEnd = doc.indexOf(QStringLiteral("</li>"), liStart);
    if (liEnd < 0) liEnd = doc.size();

    const QString needle = QStringLiteral("data-coffee-bag-payload-value=\"");
    const qsizetype pStart = doc.indexOf(needle, liStart);
    if (pStart < 0 || pStart > liEnd) return QVariantMap();
    const qsizetype valueStart = pStart + needle.size();
    const qsizetype valueEnd = doc.indexOf(QLatin1Char('"'), valueStart);
    if (valueEnd < 0) return QVariantMap();

    const QString json = htmlUnescape(doc.mid(valueStart, valueEnd - valueStart));
    const QJsonObject src = QJsonDocument::fromJson(json.toUtf8()).object();
    if (src.isEmpty()) return QVariantMap();

    // Visualizer column -> our blob key (the vocabulary downstream consumers
    // — popup, advisor block, uploads — expect). elevation is a single
    // display string here.
    static const QList<QPair<QString, QString>> kMap = {
        {QStringLiteral("roast_level"), QStringLiteral("degree")},
        {QStringLiteral("country"), QStringLiteral("origin")},
        {QStringLiteral("region"), QStringLiteral("region")},
        {QStringLiteral("farmer"), QStringLiteral("producer")},
        {QStringLiteral("variety"), QStringLiteral("variety")},
        {QStringLiteral("processing"), QStringLiteral("process")},
        {QStringLiteral("harvest_time"), QStringLiteral("harvest")},
        {QStringLiteral("tasting_notes"), QStringLiteral("tastingNotes")},
        {QStringLiteral("elevation"), QStringLiteral("elevation")},
    };
    QVariantMap out;
    for (const auto& [srcKey, outKey] : kMap) {
        const QString v = src.value(srcKey).toString();
        if (!v.isEmpty()) out[outKey] = v;
    }
    return out;
}
