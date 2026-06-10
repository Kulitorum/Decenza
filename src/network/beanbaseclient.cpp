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

// Free-tier rate contract (loffeelabs.com/developers/documentation).
constexpr int kDebounceMs = 800;
constexpr int kMinSendGapMs = 3000;
constexpr int kSearchResultLimit = 25;  // Well under the 50/call free-tier cap.

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
    return error.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive)
        ? QStringLiteral("ratelimited")
        : QStringLiteral("quota");
}
}  // namespace

BeanBaseClient::BeanBaseClient(QNetworkAccessManager* networkManager,
                               Settings* settings, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_settings(settings)
    , m_baseUrl(QString::fromLatin1(kBeanBaseBaseUrl))
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(kDebounceMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &BeanBaseClient::sendQueuedSearch);

    m_cooldownTimer.setSingleShot(true);
    connect(&m_cooldownTimer, &QTimer::timeout, this, &BeanBaseClient::sendQueuedSearch);
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

void BeanBaseClient::search(const QString& query) {
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

void BeanBaseClient::sendQueuedSearch() {
    if (m_pendingQuery.isEmpty())
        return;

    // Respect the 1-request-per-3 s free-tier window: if we sent recently,
    // park the query until the window clears (a newer search() replaces it).
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

    m_sinceLastSend.restart();
    QNetworkReply* reply = m_networkManager->get(request);
    m_activeSearchReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, query]() {
        reply->deleteLater();
        if (m_activeSearchReply == reply)
            m_activeSearchReply.clear();

        // Superseded request aborted by doSendSearch — drop silently.
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;

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
        // The id is kept as an opaque string: the user guide says "numerical"
        // but the live format is unverified (design.md § Open Questions).
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
