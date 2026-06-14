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

// Client for bean lookup via Visualizer's official canonical search API (keyless
// — see search()). The returned `id` is Visualizer's canonical UUID, the
// identity we store locally AND send back on shot PATCH so the same bag id lands
// in both systems.
//
// Endpoint (base https://visualizer.coffee — open-source miharekar/visualizer,
// Api::CanonicalCoffeeBagsController, unauthenticated, documented in openapi.yaml):
//   GET /api/canonical_coffee_bags?q=…
// The response carries every descriptive field per result, so search is a SINGLE
// request — there is no second enrichment round-trip (fetchCanonicalDetails
// re-emits the attributes already on the entry).
//
// The 350 ms debounce is plain type-ahead coalescing with a session-lifetime
// response cache; together they keep usage within the API's rate limit
// (50 req/min per IP, 200/10 min).
class BeanBaseClient : public QObject {
    Q_OBJECT

public:
    explicit BeanBaseClient(QNetworkAccessManager* networkManager,
                            Settings* settings, QObject* parent = nullptr);

    // UI search path (the Beans-page bar): Visualizer's canonical search API.
    // Keyless, substring + multi-word matching, and the returned `id` is
    // Visualizer's canonical UUID — the identity we store locally AND send back
    // on shot PATCH (`shot[canonical_coffee_bag_id]`) so the same bag id lands in
    // both systems. Results arrive via searchResults(query, entries) with
    // {id, visualizerCanonicalId, source:"visualizer", roasterName, roastName,
    // canonicalRoasterId} plus the descriptive blob (degree, origin, region,
    // producer, variety, process, harvest, tastingNotes, elevation). Debounced
    // 350 ms, latest-wins, session-cached (debounce + cache keep usage under the
    // endpoint's 50 req/min limit).
    Q_INVOKABLE void search(const QString& query);

    // Post-pick attribute enrichment. The search response already carried every
    // descriptive field on the entry, so this is a local re-emit — NO network
    // round-trip: it pulls the blob keys off `entry` and emits
    // canonicalDetails(canonicalId, attrs) on a later event-loop turn (so a
    // consumer that connects after invoking it still gets the signal). Silent
    // when the entry has no descriptive values — enrichment is best-effort.
    Q_INVOKABLE void fetchCanonicalDetails(const QVariantMap& entry);

    // Test seam: redirect requests at a local fake server. Production code
    // never calls this; the default is the live service.
    void setVisualizerBaseUrl(const QString& baseUrl) { m_visualizerBaseUrl = baseUrl; }

    // Parses the /api/canonical_coffee_bags JSON ({data:[…]}) into entries:
    // {id, visualizerCanonicalId, source:"visualizer", roasterName (from
    // canonical_roaster_name), roastName (from name), canonicalRoasterId} plus
    // the descriptive blob, remapping Visualizer column names onto our blob keys
    // (roast_level→degree, country→origin, farmer→producer, processing→process,
    // harvest_time→harvest, tasting_notes→tastingNotes; region/variety/elevation
    // kept). Empty/null values are dropped. Static + public for tests; degrades
    // to an empty list on malformed JSON (parsedOk reports parse success).
    static QVariantList parseCanonicalCoffeeBags(const QByteArray& json, bool* parsedOk = nullptr);

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

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    QString m_visualizerBaseUrl;

    // Canonical (Visualizer) debounce state — coalesces type-ahead; see search().
    QTimer m_canonicalDebounceTimer;
    QString m_pendingCanonicalQuery;
    QPointer<QNetworkReply> m_activeCanonicalReply;
    QString m_activeCanonicalQuery;  // Query of the in-flight canonical reply.

    // Session cache: normalized query -> parsed entries.
    QHash<QString, QVariantList> m_canonicalCache;
};
