#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;

// Client for bean lookup via Visualizer's canonical autocomplete (keyless —
// see search()). The returned `id` is Visualizer's canonical UUID, the
// identity we store locally AND send back on shot PATCH so the same bag id
// lands in both systems.
//
// Endpoint (base https://visualizer.coffee — open-source miharekar/visualizer,
// CanonicalController, unauthenticated):
//   GET /canonical/autocomplete_coffee_bags?q=…
//   GET /canonical/autocomplete_roasters?q=…   (two-stage enrichment)
//
// The canonical path's 350 ms debounce is plain type-ahead coalescing with a
// session-lifetime response cache; no external rate contract.
class BeanBaseClient : public QObject {
    Q_OBJECT

public:
    explicit BeanBaseClient(QNetworkAccessManager* networkManager,
                            Settings* settings, QObject* parent = nullptr);

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

    // Test seam: redirect requests at a local fake server. Production code
    // never calls this; the default is the live service.
    void setVisualizerBaseUrl(const QString& baseUrl) { m_visualizerBaseUrl = baseUrl; }

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
    // query is echoed back so a consumer can discard stale results.
    void searchResults(const QString& query, const QVariantList& entries);
    // status tokens ("network" | "parse" | "superseded") that the QML layer
    // maps to a localized string.
    void searchFailed(const QString& query, const QString& status);

    // Best-effort attribute enrichment for a previously selected entry.
    void canonicalDetails(const QString& canonicalId, const QVariantMap& attrs);

private:
    void doSendCanonicalSearch(const QString& query);
    void fetchCanonicalPayload(const QString& roasterUuid, const QVariantMap& entry);

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    QString m_visualizerBaseUrl;

    // Canonical (Visualizer) debounce state — no rate floor; see search().
    QTimer m_canonicalDebounceTimer;
    QString m_pendingCanonicalQuery;
    QPointer<QNetworkReply> m_activeCanonicalReply;
    QString m_activeCanonicalQuery;  // Query of the in-flight canonical reply.

    // Session caches: normalized query -> parsed entries; roaster name -> UUID.
    QHash<QString, QVariantList> m_canonicalCache;  // Visualizer canonical
    QHash<QString, QString> m_roasterUuidCache;
};
