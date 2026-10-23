#include "communitysettingsclient.h"

#include "httpauth.h"
#include "core/settings.h"
#include "core/settings_visualizer.h"
#include "core/settings_dye.h"
#include "core/communityaggregator.h"
#include "profile/profile.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QSet>

CommunitySettingsClient::CommunitySettingsClient(QNetworkAccessManager* networkManager,
                                                 Settings* settings, QObject* parent)
    : QObject(parent), m_networkManager(networkManager), m_settings(settings)
{
    // A base-URL change points at a different server: its cached summaries and the
    // 404 latch are both per-server and must not carry over.
    connect(m_settings->visualizer(), &SettingsVisualizer::communityApiBaseUrlChanged,
            this, &CommunitySettingsClient::resetForNewServer);
    // A grinder change makes cached summaries (ranked/grind-matched against the OLD
    // grinder) stale — drop them so the next search re-aggregates. The 404 latch is
    // per-server, not per-grinder, so it is NOT reset here.
    const auto dropCache = [this]() { m_cache.clear(); };
    connect(m_settings->dye(), &SettingsDye::dyeEquipmentNameChanged, this, dropCache);
    connect(m_settings->dye(), &SettingsDye::dyeGrinderModelChanged, this, dropCache);
    connect(m_settings->dye(), &SettingsDye::dyeGrinderBrandChanged, this, dropCache);
    // The enjoyment floor changes the ranking, so cached summaries are stale too.
    connect(m_settings->visualizer(), &SettingsVisualizer::communityMinEnjoymentChanged, this, dropCache);
    // A credential change switches accounts: cached authed summaries/profiles must not
    // survive it (sendLeg attaches auth only at request time, so a cache hit would skip
    // the check). Keyed on the two credential signals — not visualizerActiveChanged,
    // which also fires on the enabled toggle and would needlessly dump caches then.
    connect(m_settings->visualizer(), &SettingsVisualizer::visualizerUsernameChanged,
            this, &CommunitySettingsClient::resetForNewCredentials);
    connect(m_settings->visualizer(), &SettingsVisualizer::visualizerPasswordChanged,
            this, &CommunitySettingsClient::resetForNewCredentials);
}

void CommunitySettingsClient::resetForNewServer()
{
    m_cache.clear();
    m_profileCache.clear();   // shot ids are per-server; a dev id could collide with a prod id
    if (!m_apiAvailable) {
        m_apiAvailable = true;
        emit apiAvailableChanged();
    }
}

void CommunitySettingsClient::resetForNewCredentials()
{
    // A Visualizer username/password change means the cached authed summaries and
    // shot profiles belong to the PREVIOUS account — drop both and abort anything in
    // flight so an old-account reply cannot repopulate them. The 404 latch is
    // per-server, not per-account, so m_apiAvailable is deliberately left untouched
    // (unlike resetForNewServer). Whoever was waiting is told: a search as
    // "superseded" (the card re-issues it under the new account), a profile fetch as
    // "network".
    const QString abortedKey = (m_active.beanReply || m_active.canonReply) ? m_active.key : QString();
    abortActive();
    QString abortedShotId;
    if (m_profileReply) {
        QNetworkReply* r = m_profileReply;
        m_profileReply = nullptr;   // detach before abort — abort() can re-enter onProfileFinished
        abortedShotId = m_profileShotId;
        r->abort();
        r->deleteLater();
    }
    m_cache.clear();
    m_profileCache.clear();
    if (!abortedKey.isEmpty())
        emit searchFailed(abortedKey, QStringLiteral("superseded"));
    if (!abortedShotId.isEmpty())
        emit shotProfileFailed(abortedShotId, QStringLiteral("network"));
}

QString CommunitySettingsClient::searchKey(const QString& roaster, const QString& coffee,
                                           const QString& canonicalId)
{
    // '\n'-joined: roaster/coffee are free text that can contain '|' but never a
    // newline, so no two distinct inputs collide.
    return (roaster.trimmed() + QLatin1Char('\n') + coffee.trimmed()
            + QLatin1Char('\n') + canonicalId.trimmed()).toLower();
}

QString CommunitySettingsClient::resolvedBaseUrl() const
{
    if (!m_baseUrlOverride.isEmpty())
        return m_baseUrlOverride.toString();
    return m_settings->visualizer()->communityApiBaseUrl();
}

void CommunitySettingsClient::setBaseUrl(const QUrl& url)
{
    m_baseUrlOverride = url;
    resetForNewServer();        // per-server state must not survive a base change
}

QUrl CommunitySettingsClient::endpointUrl(const QString& roaster, const QString& coffee,
                                          const QString& canonicalId, bool byCanonical) const
{
    // Visualizer's community search: public shots plus the caller's own private ones,
    // full shot fields, newest first, at most 100 (full=1 allows 10 requests/min per IP).
    QUrl url(resolvedBaseUrl() + QStringLiteral("/api/shots"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("community"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("full"), QStringLiteral("1"));
    if (byCanonical) {
        q.addQueryItem(QStringLiteral("canonical_coffee_bag_id"), canonicalId);
    } else {
        q.addQueryItem(QStringLiteral("bean_brand"), roaster);
        q.addQueryItem(QStringLiteral("bean_type"), coffee);
    }
    // Deliberately NO min_enjoyment: the server would then drop below-floor and
    // unrated shots entirely, but CommunityAggregator applies communityMinEnjoyment
    // to RANKING only — below-floor/unrated shots still feed the display medians and
    // the shots-only fallback. The floor lives client-side (summarize + deep link).
    q.addQueryItem(QStringLiteral("items"), QStringLiteral("100"));
    url.setQuery(q);
    return url;
}

QNetworkReply* CommunitySettingsClient::sendLeg(const QUrl& url)
{
    QNetworkRequest request(url);
    // A server that accepts and never answers must still end in searchFailed /
    // shotProfileFailed ("network"), or the card waits forever.
    request.setTransferTimeout(30000);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // The endpoint serves public shots unauthenticated; attach Basic auth only when
    // the user has connected Visualizer, so their own + premium history is included.
    SettingsVisualizer* vis = m_settings->visualizer();
    if (vis->visualizerConnected())
        request.setRawHeader("Authorization",
                             basicAuthHeader(vis->visualizerUsername(), vis->visualizerPassword()));
    return m_networkManager->get(request);
}

void CommunitySettingsClient::search(const QString& roaster, const QString& coffee,
                                     const QString& canonicalId)
{
    const QString key = searchKey(roaster, coffee, canonicalId);

    // Endpoint already known absent this session → card stays on the deep link.
    if (!m_apiAvailable) {
        deferredFailure(key, QStringLiteral("unsupported"));
        return;
    }
    if (m_cache.contains(key)) {
        deferredResults(key, m_cache.value(key));
        return;
    }

    // Same key already in flight → coalesce onto it rather than tearing it down. The
    // key fully determines the request (roaster + coffee + canonicalId), and the live
    // legs will emit resultsReady(key)/searchFailed(key) to every key-matched listener,
    // including a consumer that just connected and called search(). Without this, the
    // MCP/ShotServer bridges (which install their listener then call search) would
    // consume the supersede below as a spurious error/409 for a request we then re-run.
    if ((m_active.beanReply || m_active.canonReply) && m_active.key == key)
        return;

    // Latest wins: a search in flight is displaced (its consumer told once). Abort
    // BEFORE emitting: the signal is synchronous, and a consumer that reacts by
    // calling search() again would otherwise have its fresh legs killed by the
    // outer frame's abort.
    if (m_active.beanReply || m_active.canonReply) {
        const QString oldKey = m_active.key;
        abortActive();
        emit searchFailed(oldKey, QStringLiteral("superseded"));
    }

    m_active = ActiveSearch{};
    m_active.key = key;
    m_active.roaster = roaster;
    m_active.coffee = coffee;
    m_active.hasCanonLeg = !canonicalId.trimmed().isEmpty();

    m_active.beanReply = sendLeg(endpointUrl(roaster, coffee, QString(), false));
    connect(m_active.beanReply, &QNetworkReply::finished, this,
            [this, reply = m_active.beanReply.data()]() { onLegFinished(reply, true); });

    if (m_active.hasCanonLeg) {
        m_active.canonReply = sendLeg(endpointUrl(QString(), QString(), canonicalId, true));
        connect(m_active.canonReply, &QNetworkReply::finished, this,
                [this, reply = m_active.canonReply.data()]() { onLegFinished(reply, false); });
    }
}

void CommunitySettingsClient::onLegFinished(QNetworkReply* reply, bool isBean)
{
    QPointer<QNetworkReply>& slot = isBean ? m_active.beanReply : m_active.canonReply;
    if (reply != slot) {            // a superseded/aborted leg reporting late
        reply->deleteLater();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const QByteArray body = reply->readAll();
    slot = nullptr;                 // detach THIS leg (so a sibling abort's reentrant finish no-ops)
    reply->deleteLater();

    // 404 on either leg: the endpoint isn't deployed here. Latch and fall back.
    if (status == 404) {
        finishUnsupported();
        return;
    }

    // Transport/HTTP failure (including 429 and other 4xx/5xx → "network").
    if (err != QNetworkReply::NoError || status != 200) {
        if (isBean) {
            const QString key = m_active.key;
            abortActive();     // bean leg is authoritative: cancel the sibling
            emit searchFailed(key, QStringLiteral("network"));
        } else {
            m_active.canon = Leg::Failed;   // enrichment leg: degrade, keep bean rows
            tryComplete();
        }
        return;
    }

    bool ok = false;
    const QVariantList rows = parseCommunityShots(body, &ok);
    if (!ok) {
        if (isBean) {
            const QString key = m_active.key;
            abortActive();
            emit searchFailed(key, QStringLiteral("parse"));
        } else {
            m_active.canon = Leg::Failed;
            tryComplete();
        }
        return;
    }

    if (isBean) {
        m_active.beanRows = rows;
        m_active.bean = Leg::Ok;
    } else {
        m_active.canonRows = rows;
        m_active.canon = Leg::Ok;
    }
    tryComplete();
}

void CommunitySettingsClient::tryComplete()
{
    if (m_active.bean != Leg::Ok)
        return;                                       // bean leg not yet ok
    if (m_active.hasCanonLeg && m_active.canon == Leg::Pending)
        return;                                       // still waiting on enrichment

    // Merge by shot id (the aggregator re-ranks, so order is irrelevant); keep all
    // bean rows, add canonical rows the bean leg didn't already carry.
    QVariantList merged = m_active.beanRows;
    QSet<QString> seen;
    for (const QVariant& r : m_active.beanRows) {
        const QString id = r.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            seen.insert(id);
    }
    for (const QVariant& r : m_active.canonRows) {
        const QString id = r.toMap().value(QStringLiteral("id")).toString();
        if (id.isEmpty() || !seen.contains(id))
            merged.append(r);
    }

    CommunityAggregator::Params params;
    // dyeEffectiveGrinder (not dyeGrinderModel): equipment-package users often
    // carry only a package display name, not a structured grinder model, so the
    // bare model string is empty for them and grinder-matching/grind/Apply were
    // all silently dead. See ADR-0020.
    params.myGrinder = m_settings->dye()->dyeEffectiveGrinder();
    params.nowEpochSec = QDateTime::currentSecsSinceEpoch();   // injected here to keep the aggregator pure
    params.minEnjoyment = m_settings->visualizer()->communityMinEnjoyment();
    const QVariantMap summary = CommunityAggregator::summarize(merged, params);

    const QString key = m_active.key;
    // Cache only a clean result, so a degraded (enrichment-failed) run can heal on retry.
    const bool clean = m_active.bean == Leg::Ok
                       && (!m_active.hasCanonLeg || m_active.canon == Leg::Ok);
    m_active = ActiveSearch{};
    if (clean)
        m_cache.insert(key, summary);
    emit resultsReady(key, summary);
}

void CommunitySettingsClient::finishUnsupported()
{
    const QString key = m_active.key;
    if (m_apiAvailable) {
        m_apiAvailable = false;
        emit apiAvailableChanged();
    }
    abortActive();
    emit searchFailed(key, QStringLiteral("unsupported"));
}

void CommunitySettingsClient::abortActive()
{
    for (QPointer<QNetworkReply>* slot : {&m_active.beanReply, &m_active.canonReply}) {
        if (*slot) {
            QNetworkReply* r = *slot;
            *slot = nullptr;        // detach BEFORE abort — abort() can re-enter onLegFinished synchronously
            r->abort();
            r->deleteLater();
        }
    }
    m_active = ActiveSearch{};
}

void CommunitySettingsClient::deferredResults(const QString& key, const QVariantMap& summary)
{
    // Emit on a later event-loop turn so a consumer that connects after invoking
    // search() still receives the terminal signal (same rationale as a cache re-emit).
    QTimer::singleShot(0, this, [this, key, summary]() { emit resultsReady(key, summary); });
}

void CommunitySettingsClient::deferredFailure(const QString& key, const QString& status)
{
    QTimer::singleShot(0, this, [this, key, status]() { emit searchFailed(key, status); });
}

void CommunitySettingsClient::fetchShotProfile(const QString& shotId)
{
    if (shotId.isEmpty())
        return;
    if (m_profileCache.contains(shotId)) {
        const QVariantMap cached = m_profileCache.value(shotId);
        QTimer::singleShot(0, this, [this, shotId, cached]() { emit shotProfileReady(shotId, cached); });
        return;
    }
    // Latest-wins: a newer profile fetch displaces the prior one.
    if (m_profileReply) {
        QNetworkReply* r = m_profileReply;
        const QString oldId = m_profileShotId;
        m_profileReply = nullptr;       // detach before abort (reentrant finish no-ops)
        r->abort();
        r->deleteLater();
        emit shotProfileFailed(oldId, QStringLiteral("superseded"));
    }
    m_profileShotId = shotId;
    QUrl url(resolvedBaseUrl() + QStringLiteral("/api/shots/") + shotId + QStringLiteral("/profile"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);
    m_profileReply = sendLeg(url);
    connect(m_profileReply, &QNetworkReply::finished, this,
            [this, reply = m_profileReply.data(), shotId]() { onProfileFinished(reply, shotId); });
}

void CommunitySettingsClient::onProfileFinished(QNetworkReply* reply, const QString& shotId)
{
    if (reply != m_profileReply) {      // a superseded/aborted fetch reporting late
        reply->deleteLater();
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const QByteArray body = reply->readAll();
    m_profileReply = nullptr;
    reply->deleteLater();

    if (err != QNetworkReply::NoError || status != 200) {
        // The /api/shots/:id/profile endpoint exists on stock Visualizer, so a
        // non-200 means the shot/profile is unavailable, not that the community
        // endpoint is absent — a transient "network", never the apiAvailable latch.
        emit shotProfileFailed(shotId, QStringLiteral("network"));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || doc.isNull()) {
        emit shotProfileFailed(shotId, QStringLiteral("parse"));
        return;
    }
    const Profile prof = Profile::fromJson(doc);
    // A 200 with a profile that can't run (no steps, unknown keys) would be stored on
    // the recipe and break its activation; treat it as unparseable.
    if (!prof.isValid()) {
        emit shotProfileFailed(shotId, QStringLiteral("parse"));
        return;
    }
    QVariantMap result;
    result[QStringLiteral("temperatureC")] = prof.espressoTemperature();
    result[QStringLiteral("profileJson")] = QString::fromUtf8(body);
    result[QStringLiteral("title")] = prof.title();
    m_profileCache.insert(shotId, result);
    emit shotProfileReady(shotId, result);
}

QUrl CommunitySettingsClient::communityPageUrl(const QString& roaster, const QString& coffee) const
{
    QUrl url(resolvedBaseUrl() + QStringLiteral("/community"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("commit"), QStringLiteral("Search"));
    q.addQueryItem(QStringLiteral("bean_brand"), roaster);
    q.addQueryItem(QStringLiteral("bean_type"), coffee);
    q.addQueryItem(QStringLiteral("min_enjoyment"),
                   QString::number(m_settings->visualizer()->communityMinEnjoyment()));
    url.setQuery(q);
    return url;
}

QVariantList CommunitySettingsClient::parseCommunityShots(const QByteArray& json, bool* ok)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    return doc.object().value(QStringLiteral("data")).toArray().toVariantList();
}
