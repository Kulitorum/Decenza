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

// Client for the Loffee Labs Bean Base API (loffeelabs.com).
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
// These windows are wall-clock requirements imposed by the external API's
// rate contract — inherently temporal, like polling or heartbeats, not
// event-suppression guards — which is why QTimer is the correct tool here
// despite the project's no-timers-as-guards rule.
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

    // Debounced, rate-limited, cached full-text search against GET /beans.
    // Results arrive via searchResults(query, entries); entries is a
    // QVariantList of QVariantMaps (QML-friendly), one per bean — see
    // parseBeans() for the field set. Failures arrive via searchFailed().
    // Empty/whitespace queries are ignored.
    Q_INVOKABLE void search(const QString& query);

    // Test seam: redirect requests at a local fake server. Production code
    // never calls this; the default is the live Bean Base API.
    void setBaseUrl(const QString& baseUrl) { m_baseUrl = baseUrl; }

    // Parses a Bean Base response body ({"data":[…]}) into QML-consumable
    // maps. Static + public so the parse layer is unit-testable in isolation.
    // Tolerant of missing fields and of tag fields arriving as either JSON
    // arrays or comma-joined strings; never fails the whole response on one
    // malformed entry.
    static QVariantList parseBeans(const QByteArray& responseBody);

signals:
    void apiKeyTestResult(bool success, const QString& message);

    // query is echoed back so a consumer can discard stale results.
    void searchResults(const QString& query, const QVariantList& entries);
    // status tokens mirror apiKeyTestResult's.
    void searchFailed(const QString& query, const QString& status);

private:
    QString apiKey() const;
    void sendQueuedSearch();   // Fires the queued query if the rate window allows.
    void doSendSearch(const QString& query);

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    Settings* m_settings = nullptr;                     // Non-owning
    QString m_baseUrl;

    // Debounce + rate-limit state. m_pendingQuery is latest-wins: a new
    // search() during the debounce or cooldown replaces it.
    QTimer m_debounceTimer;
    QTimer m_cooldownTimer;
    QElapsedTimer m_sinceLastSend;   // Invalid until the first send.
    QString m_pendingQuery;
    QPointer<QNetworkReply> m_activeSearchReply;  // Aborted when superseded.

    // Session cache: normalized query -> parsed entries.
    QHash<QString, QVariantList> m_cache;
};
