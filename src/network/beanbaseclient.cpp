#include "beanbaseclient.h"

#include "../core/settings.h"
#include "../core/settings_beanbase.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
// Confirmed June 2026 by probing the public endpoints — see design.md.
// `/beans` requires auth; `/roasters|/origins|/varieties|/processes` are public.
constexpr auto kBeanBaseBaseUrl = "https://loffeelabs.com/api/v2";

// Visualizer's canonical autocomplete (open-source: miharekar/visualizer,
// CanonicalController — unauthenticated, substring + multi-word matching,
// returns the canonical UUID we store locally AND send on shot PATCH).
// Internal endpoint: parse defensively, degrade silently.
constexpr auto kVisualizerBaseUrl = "https://visualizer.coffee";

// Free-tier rate contract (loffeelabs.com/developers/documentation).
constexpr int kDebounceMs = 800;
constexpr int kMinSendGapMs = 3000;
constexpr int kSearchResultLimit = 25;  // Well under the 50/call free-tier cap.

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

// Tag fields ("tasting-tag", "general-tag") have been observed only in the
// docs' export list, not in a live authenticated payload — accept both a
// JSON array and a comma-joined string.
QStringList toTagList(const QJsonValue& v) {
    if (v.isArray()) {
        QStringList out;
        const QJsonArray arr = v.toArray();
        for (const QJsonValue& item : arr) {
            const QString s = item.toString().trimmed();
            if (!s.isEmpty()) out.append(s);
        }
        return out;
    }
    QStringList out;
    const QStringList parts = v.toString().split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString s = part.trimmed();
        if (!s.isEmpty()) out.append(s);
    }
    return out;
}

// id may be a JSON number or string; elevations may be numeric or string.
QString toIdString(const QJsonValue& v) {
    if (v.isDouble()) return QString::number(static_cast<qint64>(v.toDouble()));
    return v.toString();
}

int toIntLoose(const QJsonValue& v) {
    if (v.isDouble()) return static_cast<int>(v.toDouble());
    return v.toString().toInt();
}

// Both the 1-req/3 s rate limit AND the 2,000-beans/day quota return 429;
// only the body text distinguishes them (rate limit: "Rate limit exceeded.
// Maximum 1 request(s) per 3 second(s)."). A quota-exhausted user must see
// "done for today", not "try again shortly" — they'd think search is broken.
QString classify429(const QByteArray& body) {
    const QString error = QJsonDocument::fromJson(body)
                              .object().value(QStringLiteral("error")).toString();
    // Match both phrasings positively; an UNRECOGNIZED body (proxy error page,
    // upstream copy change) defaults to the recoverable reading — telling a
    // user "done for today" on a transient 429 makes them give up for a day.
    if (error.contains(QStringLiteral("quota"), Qt::CaseInsensitive)
        || error.contains(QStringLiteral("daily"), Qt::CaseInsensitive))
        return QStringLiteral("quota");
    return QStringLiteral("ratelimited");
}
}  // namespace

BeanBaseClient::BeanBaseClient(QNetworkAccessManager* networkManager,
                               Settings* settings, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_settings(settings)
    , m_baseUrl(QString::fromLatin1(kBeanBaseBaseUrl))
    , m_visualizerBaseUrl(QString::fromLatin1(kVisualizerBaseUrl))
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(kDebounceMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &BeanBaseClient::sendQueuedSearch);

    m_cooldownTimer.setSingleShot(true);
    connect(&m_cooldownTimer, &QTimer::timeout, this, &BeanBaseClient::sendQueuedSearch);

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

QString BeanBaseClient::apiKey() const {
    return m_settings ? m_settings->beanbase()->beanBaseApiKey() : QString();
}

void BeanBaseClient::testApiKey() {
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit apiKeyTestResult(false, QStringLiteral("missing"));
        return;
    }
    if (!m_networkManager) {
        emit apiKeyTestResult(false, QStringLiteral("network"));
        return;
    }

    // A 1-bean fetch is the cheapest authenticated call; 200 proves the key works.
    QNetworkRequest request{QUrl(QStringLiteral("%1/beans?limit=1").arg(m_baseUrl))};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() == QNetworkReply::NoError && status == 200) {
            emit apiKeyTestResult(true, QStringLiteral("success"));
        } else if (status == 401) {
            emit apiKeyTestResult(false, QStringLiteral("invalid"));
        } else if (status == 429) {
            emit apiKeyTestResult(false, classify429(reply->readAll()));
        } else {
            // Transport failure or unexpected status — treat as "couldn't reach".
            emit apiKeyTestResult(false, QStringLiteral("network"));
        }
    });
}

void BeanBaseClient::searchBeanBase(const QString& query) {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return;

    const QString normalized = trimmed.toLower();
    const auto cached = m_cache.constFind(normalized);
    if (cached != m_cache.constEnd()) {
        emit searchResults(trimmed, cached.value());
        return;
    }

    // Latest-wins: replace any queued query and restart the debounce window.
    m_pendingQuery = trimmed;
    m_cooldownTimer.stop();
    m_debounceTimer.start();
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, canonicalId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;  // Best-effort: transient failures stay silent.
        const QVariantMap attrs = parseCanonicalPayload(reply->readAll(), canonicalId);
        if (!attrs.isEmpty())
            emit canonicalDetails(canonicalId, attrs);
        else
            qWarning() << "BeanBaseClient: enrichment payload missing/unparseable for" << canonicalId;
    });
}

void BeanBaseClient::sendQueuedSearch() {
    if (m_pendingQuery.isEmpty())
        return;

    // Respect the 1-request-per-3 s free-tier window: if we sent recently,
    // park the query until the window clears (a newer searchBeanBase() replaces it).
    if (m_sinceLastSend.isValid()) {
        const qint64 elapsed = m_sinceLastSend.elapsed();
        if (elapsed < kMinSendGapMs) {
            m_cooldownTimer.start(static_cast<int>(kMinSendGapMs - elapsed));
            return;
        }
    }

    const QString query = m_pendingQuery;
    m_pendingQuery.clear();
    doSendSearch(query);
}

void BeanBaseClient::doSendSearch(const QString& query) {
    const QString key = apiKey();
    if (key.isEmpty()) {
        emit searchFailed(query, QStringLiteral("missing"));
        return;
    }
    if (!m_networkManager) {
        emit searchFailed(query, QStringLiteral("network"));
        return;
    }

    // A newer query supersedes any in-flight one — abort it so slow responses
    // can't arrive out of order on top of fresher results.
    if (m_activeSearchReply) {
        m_activeSearchReply->abort();
        m_activeSearchReply.clear();
    }

    QUrl url(QStringLiteral("%1/beans").arg(m_baseUrl));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("search"), query);
    urlQuery.addQueryItem(QStringLiteral("limit"), QString::number(kSearchResultLimit));
    url.setQuery(urlQuery);

    QNetworkRequest request{url};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());

    request.setTransferTimeout(kTransferTimeoutMs);
    m_sinceLastSend.restart();
    QNetworkReply* reply = m_networkManager->get(request);
    m_activeSearchReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        reply->deleteLater();
        const bool wasActive = (m_activeSearchReply == reply);
        if (wasActive)
            m_activeSearchReply.clear();

        // Superseded request aborted by doSendSearch — drop silently; a
        // transfer TIMEOUT (reply still active) is a real failure.
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            if (wasActive)
                emit searchFailed(query, QStringLiteral("network"));
            return;
        }

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status == 200) {
            const QVariantList entries = parseBeans(reply->readAll());
            m_cache.insert(query.toLower(), entries);
            emit searchResults(query, entries);
        } else if (status == 401) {
            emit searchFailed(query, QStringLiteral("invalid"));
        } else if (status == 429) {
            emit searchFailed(query, classify429(reply->readAll()));
        } else {
            emit searchFailed(query, QStringLiteral("network"));
        }
    });
}

QVariantList BeanBaseClient::parseBeans(const QByteArray& responseBody) {
    QVariantList out;
    const QJsonDocument doc = QJsonDocument::fromJson(responseBody);

    // Confirmed against the live API (June 2026): the authenticated /beans
    // endpoint wraps results as {"meta":{…},"beans":[…]} — only the public
    // endpoints (/roasters etc.) use {"data":[…]}. Tolerate both plus a bare
    // array.
    QJsonArray beans;
    if (doc.isObject()) {
        beans = doc.object().value(QStringLiteral("beans")).toArray();
        if (beans.isEmpty())
            beans = doc.object().value(QStringLiteral("data")).toArray();
    } else if (doc.isArray()) {
        beans = doc.array();
    }

    for (const QJsonValue& v : std::as_const(beans)) {
        if (!v.isObject())
            continue;
        const QJsonObject bean = v.toObject();

        QVariantMap entry;
        // Path discriminant — canonical entries carry source:"visualizer";
        // every entry self-identifies so shape divergence is checkable.
        entry[QStringLiteral("source")] = QStringLiteral("beanbase");
        // The id is kept as an opaque string (confirmed numeric on the live
        // API, June 2026).
        entry[QStringLiteral("id")] = toIdString(bean.value(QStringLiteral("id")));
        entry[QStringLiteral("roasterName")] = bean.value(QStringLiteral("roaster")).toString();
        entry[QStringLiteral("roastName")] = bean.value(QStringLiteral("roast-name")).toString();
        entry[QStringLiteral("degree")] = bean.value(QStringLiteral("degree")).toString();
        entry[QStringLiteral("beanType")] = bean.value(QStringLiteral("type")).toString();
        entry[QStringLiteral("link")] = bean.value(QStringLiteral("link")).toString();
        entry[QStringLiteral("image")] = bean.value(QStringLiteral("image")).toString();
        entry[QStringLiteral("origin")] = bean.value(QStringLiteral("origin")).toString();
        entry[QStringLiteral("region")] = bean.value(QStringLiteral("region")).toString();
        entry[QStringLiteral("producer")] = bean.value(QStringLiteral("producer")).toString();
        entry[QStringLiteral("variety")] = bean.value(QStringLiteral("variety")).toString();
        entry[QStringLiteral("process")] = bean.value(QStringLiteral("process")).toString();
        entry[QStringLiteral("description")] = bean.value(QStringLiteral("description")).toString();
        entry[QStringLiteral("tastingNotes")] = bean.value(QStringLiteral("tasting")).toString();
        entry[QStringLiteral("harvest")] = bean.value(QStringLiteral("harvest")).toString();
        entry[QStringLiteral("minElevationM")] = toIntLoose(bean.value(QStringLiteral("min-elev")));
        entry[QStringLiteral("maxElevationM")] = toIntLoose(bean.value(QStringLiteral("max-elev")));
        entry[QStringLiteral("tastingTags")] = toTagList(bean.value(QStringLiteral("tasting-tag")));
        entry[QStringLiteral("generalTags")] = toTagList(bean.value(QStringLiteral("general-tag")));
        entry[QStringLiteral("roasterRegion")] = bean.value(QStringLiteral("roaster-region")).toString();
        entry[QStringLiteral("roasterCountry")] = bean.value(QStringLiteral("roaster-country")).toString();
        entry[QStringLiteral("soldout")] = bean.value(QStringLiteral("soldout")).toBool(false);
        entry[QStringLiteral("available")] = bean.value(QStringLiteral("available")).toBool(true);

        out.append(entry);
    }
    return out;
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

    // Visualizer column -> our blob key (same vocabulary parseBeans emits,
    // so downstream consumers — popup, advisor block, uploads — are
    // source-agnostic). elevation is a single display string here.
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
