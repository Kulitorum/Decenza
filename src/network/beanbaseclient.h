#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// Client for bean lookup, two paths: Visualizer's canonical autocomplete
// (primary, keyless — see search()) and the Loffee Labs Bean Base API
// (optional, key-gated — see searchBeanBase()).
//
// Endpoints (base https://loffeelabs.com/api/v2, confirmed June 2026 — see
// openspec/changes/add-bean-base-integration/design.md):
//   GET /beans?search=…&limit=…   authenticated (Authorization: Bearer <key>)
//   GET /roasters|/origins|/varieties|/processes   public
//
// The API key is read live from `Settings.beanbase.beanBaseApiKey` on each
// request, so a key change in the Settings UI takes effect immediately.
//
// Free-tier budget: 1 request / 3 s, 2,000 beans/day, 50 beans per call.
// search() enforces that budget client-side:
//   - 800 ms debounce after the last keystroke (latest query wins),
//   - at least 3 s between *sent* requests (queued query replaced, never
//     accumulated),
//   - session-lifetime response cache keyed by normalized query (a repeated
//     query never re-spends quota).
// Those Bean Base windows are wall-clock requirements imposed by the external
// API's rate contract — inherently temporal, like polling or heartbeats, not
// event-suppression guards — which is why QTimer is the correct tool there
// despite the project's no-timers-as-guards rule. (The canonical path's
// 350 ms debounce is plain type-ahead coalescing; no external contract.)
class BeanBaseClient : public QObject {
    Q_OBJECT

public:
    explicit BeanBaseClient(QNetworkAccessManager* networkManager,
                            Settings* settings, QObject* parent = nullptr);

    // Validates the currently-configured API key by issuing `GET /beans?limit=1`.
    // Emits apiKeyTestResult(success, message) when the response arrives.
    // message is a status token ("success" | "invalid" | "ratelimited" |
    // "network" | "missing") that the QML layer maps to a localized string.
    Q_INVOKABLE void testApiKey();

    // UI search path (the Beans-page bar): Visualizer's canonical
    // autocomplete. Keyless, substring + multi-word matching, and the
    // returned `id` is Visualizer's canonical UUID — the identity we store
    // locally AND send back on shot PATCH (`shot[canonical_coffee_bag_id]`)
    // so the same bag id lands in both systems. Results arrive via
    // searchResults(query, entries) with {id, visualizerCanonicalId,
    // source:"visualizer", roasterName, roastName}. Debounced 350 ms,
    // latest-wins, session-cached; no rate-limit floor (no documented limit
    // on this endpoint; debounce + cache keep usage polite).
    Q_INVOKABLE void search(const QString& query);

    // Post-pick attribute enrichment (two-stage canonical flow): resolves
    // the roaster UUID by exact name, then re-queries with
    // require_roaster=true to get the embedded attribute payload
    // (roast_level, country, region, farmer, variety, processing,
    // harvest_time, elevation, tasting_notes). Emits
    // canonicalDetails(canonicalId, attrs) on success; silent on failure —
    // enrichment is best-effort on top of an already-stored link.
    Q_INVOKABLE void fetchCanonicalDetails(const QVariantMap& entry);

    // The Bean Base API proper — currently UNUSED in production (kept for
    // optional key-gated enrichment; the UI and the `bean_search` MCP tool
    // both use the canonical search() above). Debounced 800 ms with the
    // documented 1-req/3 s floor and daily quota; whole-word matching;
    // requires the user's API key. Same searchResults/searchFailed signals
    // (filter by query echo).
    Q_INVOKABLE void searchBeanBase(const QString& query);

    // Test seams: redirect requests at a local fake server. Production code
    // never calls these; defaults are the live services.
    void setBaseUrl(const QString& baseUrl) { m_baseUrl = baseUrl; }
    void setVisualizerBaseUrl(const QString& baseUrl) { m_visualizerBaseUrl = baseUrl; }

    // Parses a Bean Base response body ({"data":[…]}) into QML-consumable
    // maps. Static + public so the parse layer is unit-testable in isolation.
    // Tolerant of missing fields and of tag fields arriving as either JSON
    // arrays or comma-joined strings; never fails the whole response on one
    // malformed entry.
    static QVariantList parseBeans(const QByteArray& responseBody);

    // Parses Visualizer's canonical autocomplete HTML fragment into entries
    // ({id, visualizerCanonicalId, source, roasterName, roastName} — plus
    // roasterWebsite for roaster fragments). Static + public for tests;
    // defensive (internal endpoint, markup may drift).
    static QVariantList parseCanonicalAutocomplete(const QByteArray& html);

    // Extracts the embedded data-coffee-bag-payload-value JSON for the row
    // matching canonicalId (the two-stage require_roaster=true response) and
    // maps Visualizer column names onto our blob keys (roast_level→degree,
    // country→origin, farmer→producer, processing→process, harvest_time→
    // harvest, tasting_notes→tastingNotes, elevation kept as a string).
    // Empty map when absent/unparseable.
    static QVariantMap parseCanonicalPayload(const QByteArray& html, const QString& canonicalId);

signals:
    void apiKeyTestResult(bool success, const QString& message);

    // query is echoed back so a consumer can discard stale results.
    void searchResults(const QString& query, const QVariantList& entries);
    // status tokens mirror apiKeyTestResult's.
    void searchFailed(const QString& query, const QString& status);

    // Best-effort attribute enrichment for a previously selected entry.
    void canonicalDetails(const QString& canonicalId, const QVariantMap& attrs);

private:
    QString apiKey() const;
    void sendQueuedSearch();   // Bean Base: fires the queued query if the rate window allows.
    void doSendSearch(const QString& query);
    void doSendCanonicalSearch(const QString& query);
    void fetchCanonicalPayload(const QString& roasterUuid, const QVariantMap& entry);

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    Settings* m_settings = nullptr;                     // Non-owning
    QString m_baseUrl;
    QString m_visualizerBaseUrl;

    // Bean Base debounce + rate-limit state. m_pendingQuery is latest-wins:
    // a new search during the debounce or cooldown replaces it.
    QTimer m_debounceTimer;
    QTimer m_cooldownTimer;
    QElapsedTimer m_sinceLastSend;   // Invalid until the first send.
    QString m_pendingQuery;
    QPointer<QNetworkReply> m_activeSearchReply;  // Aborted when superseded.

    // Canonical (Visualizer) debounce state — no rate floor; see search().
    QTimer m_canonicalDebounceTimer;
    QString m_pendingCanonicalQuery;
    QPointer<QNetworkReply> m_activeCanonicalReply;
    QString m_activeCanonicalQuery;  // Query of the in-flight canonical reply.

    // Session caches: normalized query -> parsed entries; roaster name -> UUID.
    QHash<QString, QVariantList> m_cache;           // Bean Base
    QHash<QString, QVariantList> m_canonicalCache;  // Visualizer canonical
    QHash<QString, QString> m_roasterUuidCache;
};
