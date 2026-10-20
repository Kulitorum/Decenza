#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <QtQmlIntegration/qqmlintegration.h>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// Bean Search — Phase 4. Despite the name (frozen by the design of record), this
// fetches community SHOTS for a bean from Visualizer's GET /api/community/shots
// (the Phase-3 endpoint), runs them through CommunityAggregator::summarize, and
// emits a ready-to-render summary. The card consumes it; MainController owns it.
//
// It deliberately does NOT depend on VisualizerUploader: it builds the request
// itself against `communityApiBaseUrl` (so a local dev server is reachable, which
// makeApiJsonRequest's hardcoded base is not) and reuses only basicAuthHeader()
// for credentials. No debounce — the trigger is a discrete bean change, not
// type-ahead — just latest-wins abort + a session cache. No request pacing: there
// is no automatic prefetch here, and any 429 from the real host already feeds the
// shared network manager's global backoff. The endpoint allows unauthenticated
// access (public shots), so the Authorization header is attached only when the
// user has connected Visualizer; otherwise the request is anonymous.
//
// Upstream-clean: own file, standard Qt, zero src/barista. Deferred to Phase 5:
// temperature from the shot's profile, the Apply path, and prefetch pacing.
class CommunitySettingsClient : public QObject {
    Q_OBJECT

    // Compile-time QML registration so qmllint/qmlcachegen/the language server can
    // follow MainController.communitySettings through to this class (a runtime
    // qmlRegister* would be invisible to all three).
    QML_ELEMENT
    QML_UNCREATABLE("Created in C++ and reached via MainController")

    // Latches false the first time the endpoint 404s (a Visualizer without the
    // Phase-3 endpoint); the card then falls back to the deep link + own history.
    Q_PROPERTY(bool apiAvailable READ apiAvailable NOTIFY apiAvailableChanged FINAL)

public:
    explicit CommunitySettingsClient(QNetworkAccessManager* networkManager,
                                     Settings* settings, QObject* parent = nullptr);

    bool apiAvailable() const { return m_apiAvailable; }

    // Search community shots for a bean. roaster/coffee are substring filters;
    // canonicalId (optional) adds a second exact-match request merged by shot id.
    // Latest-wins: a new search supersedes any in flight. Session-cached by
    // searchKey(). Emits resultsReady(key, summary) or searchFailed(key, status).
    Q_INVOKABLE void search(const QString& roaster, const QString& coffee,
                            const QString& canonicalId);

    // The "Open on Visualizer" deep link — the single producer of this URL (the
    // card no longer builds its own). Uses the configured base + min-enjoyment
    // floor so a dev server and the floor setting are honoured here too.
    Q_INVOKABLE QUrl communityPageUrl(const QString& roaster, const QString& coffee) const;

    // Fetch one community shot's profile (GET /api/shots/:id/profile?format=json) —
    // Phase-5 Apply needs its temperature (not a shot-list column) and its JSON to
    // install the profile if the user doesn't have it. Latest-wins, session-cached by
    // shot id. Emits shotProfileReady(shotId, {temperatureC, profileJson, title}) or
    // shotProfileFailed(shotId, status). Lazy (called on the Apply tap), never on render.
    Q_INVOKABLE void fetchShotProfile(const QString& shotId);

    // The cache / signal-echo key: trimmed, lower-cased, '\n'-joined (roaster and
    // coffee are free text and may contain '|' but never a newline). Public static
    // + invokable so the card composes the same key to match echoed replies.
    Q_INVOKABLE static QString searchKey(const QString& roaster, const QString& coffee,
                                         const QString& canonicalId);

    // Parse the endpoint envelope ({data:[…]}) into a list of shot QVariantMaps.
    // Static + public for tests; *ok reports JSON parse success (false → caller
    // maps to a "parse" failure, never an empty result).
    static QVariantList parseCommunityShots(const QByteArray& json, bool* ok = nullptr);

    // Test seam: redirect requests at a local fake server. When set (non-empty),
    // overrides communityApiBaseUrl. Resets the availability latch and clears the
    // session cache (cached results are per-server). Production never calls this.
    void setBaseUrl(const QUrl& url);

signals:
    // key is echoed so the card can discard a reply for a bean it no longer shows.
    void resultsReady(const QString& key, const QVariantMap& summary);
    // status: "network" | "parse" | "superseded" | "unsupported".
    void searchFailed(const QString& key, const QString& status);
    void apiAvailableChanged();

    // result = {temperatureC: double, profileJson: QString, title: QString}.
    void shotProfileReady(const QString& shotId, const QVariantMap& result);
    void shotProfileFailed(const QString& shotId, const QString& status); // network|parse|superseded


private:
    // Per-leg outcome, so completion can degrade (canonical leg is enrichment;
    // the bean-filter leg is authoritative).
    enum class Leg { Pending, Ok, Failed };

    struct ActiveSearch {
        QString key;
        QString roaster;
        QString coffee;
        bool hasCanonLeg = false;
        QPointer<QNetworkReply> beanReply;
        QPointer<QNetworkReply> canonReply;
        Leg bean = Leg::Pending;
        Leg canon = Leg::Pending;   // stays Pending/Ok/Failed; irrelevant when !hasCanonLeg
        QVariantList beanRows;
        QVariantList canonRows;
    };

    QNetworkReply* sendLeg(const QUrl& url);
    void onLegFinished(QNetworkReply* reply, bool isBean);
    void tryComplete();
    void finishUnsupported();     // 404 latch path
    void abortActive();           // detach-then-abort both legs, clear m_active (never emits)
    void resetForNewServer();     // clear per-server cache + un-latch (base URL changed)
    void resetForNewCredentials();// clear authed caches + abort in flight (account changed); latch untouched
    QString resolvedBaseUrl() const;
    QUrl endpointUrl(const QString& roaster, const QString& coffee,
                     const QString& canonicalId, bool byCanonical) const;
    void deferredResults(const QString& key, const QVariantMap& summary);
    void deferredFailure(const QString& key, const QString& status);
    void onProfileFinished(QNetworkReply* reply, const QString& shotId);

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    Settings* m_settings = nullptr;                     // Non-owning
    QUrl m_baseUrlOverride;                              // test seam
    bool m_apiAvailable = true;

    ActiveSearch m_active;
    QHash<QString, QVariantMap> m_cache;  // searchKey -> summary (summary carries its own shots)

    QPointer<QNetworkReply> m_profileReply; // latest-wins single-shot profile fetch (Apply)
    QString m_profileShotId;
    QHash<QString, QVariantMap> m_profileCache;  // shotId -> {temperatureC, profileJson, title}
};
