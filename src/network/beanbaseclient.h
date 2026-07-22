#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QPointer>
#include <QSet>
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

    // --- Bag image file cache (pixels never enter the database) ---
    // The canonical DB has no image column, but entries generally carry the
    // roaster's product-page URL. ensureBagImage() resolves a bag photo
    // best-effort: product page → og:image meta tag → download → file in the
    // app cache directory keyed by canonical id (size-capped, oldest-first
    // eviction by write time; evictable and re-resolvable, so it is a cache,
    // not data). When productUrl is empty (a blob linked before `link` was
    // captured, or an entry the API served without a url), the URL is first
    // recovered by re-searching the canonical API by roastName and matching
    // the id — a recovered URL is also announced via bagLinkRecovered so
    // consumers can backfill the blob. One attempt per canonical id per app
    // session; silent on every failure path (no og:image, network error, page
    // gone) — consumers keep their placeholder; only local disk faults warn.
    // Emits bagImageReady(canonicalId, filePath) on success, and re-emits it
    // (deferred) when the file already exists.
    Q_INVOKABLE QString bagImagePath(const QString& canonicalId) const;
    Q_INVOKABLE void ensureBagImage(const QString& canonicalId,
                                    const QString& roastName,
                                    const QString& productUrl);
    // The user edited the bag's product URL: re-resolve from the new URL,
    // overwriting the cached image (add-bag-detail-editing). ensureBagImage()
    // alone would take its cache-hit branch and keep serving the stale pixels.
    //
    // Resolve-THEN-swap: the old file stays in place until the new bytes land
    // (downloadBagImage commits through QSaveFile, which replaces the target
    // atomically — and, unlike QFile::rename, will replace one that already
    // exists, which is what keeping the old file requires). Evicting
    // up front left the bag with NO photo for the length of a network round
    // trip, which a consumer that re-reads immediately — the web /beans grid
    // reloads the instant the save returns — renders as the photo vanishing on
    // save, and permanently if the new page has no og:image. A stale photo is
    // the lesser wrong, and it is what the user already had.
    Q_INVOKABLE void refreshBagImage(const QString& canonicalId,
                                     const QString& roastName,
                                     const QString& productUrl);
    // Recover the product URL for a blob that lacks `link`, independent of the
    // image state (a bag whose image is already cached still needs its reorder
    // URL). Re-searches the canonical API by roastName, matches the id, and
    // emits bagLinkRecovered on success. One attempt per id per session;
    // silent on failure. ensureBagImage routes its legacy branch through this.
    Q_INVOKABLE void recoverBagLink(const QString& canonicalId, const QString& roastName);

    // Validate a bag's stored product URL once (pick-time). Follows redirects
    // and reports the outcome so the consumer can persist "the right data":
    //   • 200 (incl. via redirect) → bagLinkResolved(id, finalUrl): a stale
    //     Shopify handle alias is normalized to the durable canonical URL.
    //   • confirmed 404/410        → bagLinkDead(id): the roaster removed the
    //     page; the consumer clears the dead link (and marks it so recovery
    //     never re-adds the same dead URL from the canonical API).
    //   • transient error (timeout/DNS/5xx) → neither, so it can retry later.
    // One GET per canonical id per session; BagCard additionally gates on a
    // persisted linkChecked marker so it is genuinely once-per-bag, not a
    // per-view probe. Independent of the image cache, so previewing a result
    // (which caches the photo) can't cause the validation to be skipped.
    Q_INVOKABLE void validateBagLink(const QString& canonicalId, const QString& productUrl);

    // --- Blob edit helpers (add-bag-detail-editing) ---
    // Thin QML bridges over the header-only BeanBaseBlob helpers so the bag
    // editor and MCP bag_update share ONE merge/revert implementation. Pure
    // string→string; no instance state.
    Q_INVOKABLE static QString mergeBeanDetails(const QString& blob, const QVariantMap& edits);
    Q_INVOKABLE static QString revertToCanonical(const QString& blob);
    Q_INVOKABLE static bool blobDiffersFromCanonical(const QString& blob);

    // Fetch a roaster product page and reduce it to plain text for the
    // "Get info" AI extraction — the same reduction Visualizer's scraper
    // performs (drop script/style/svg/img, strip tags, squish). Follows
    // redirects; emits pageTextReady/pageTextFailed.
    Q_INVOKABLE void fetchPageText(const QString& url);
    // The HTML -> squished-plain-text reduction. Static + public for tests.
    static QString extractPageText(const QByteArray& html);

    // og:image URL extraction from product-page HTML (property= or name=,
    // og:image:secure_url variant, either attribute order; protocol-relative
    // URLs normalized to https). Empty when absent or not an absolute http(s)
    // URL. Static + public for tests.
    static QString extractOgImage(const QByteArray& html);

    // Stage-2 extraction photo (add-recipe-wizard-tea): the model returned
    // the product photo's URL directly (no og:image on JS-rendered shops).
    // Downloads it into the same cache the og:image pipeline fills; a cache
    // hit is left alone. Emits bagImageReady on success like every other path.
    Q_INVOKABLE void cacheBagImageFromUrl(const QString& imageKey, const QString& imageUrl);
    // Same, but REPLACES an existing cache entry. cacheBagImageFromUrl lets a
    // cache hit win, which is right when warming a bag that has no photo yet
    // and wrong when the user just changed the product URL — the cached pixels
    // describe the old page and the stage-2 photo is the only one this shop
    // will ever yield (an SPA has no og:image to re-scrape). The write itself
    // is atomic, so the old photo survives until the new bytes land.
    Q_INVOKABLE void replaceBagImageFromUrl(const QString& imageKey, const QString& imageUrl);

    // Test seam: redirect requests at a local fake server. Production code
    // never calls this; the default is the live service.
    void setVisualizerBaseUrl(const QString& baseUrl) { m_visualizerBaseUrl = baseUrl; }
    // Test seam: cache directory override (default: CacheLocation/bagimages).
    void setImageCacheDir(const QString& dir) { m_imageCacheDir = dir; }

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

    // A bag photo landed in (or already existed in) the file cache.
    void bagImageReady(const QString& canonicalId, const QString& filePath);
    // "Get info" page fetch (add-bag-detail-editing): the product page's
    // plain text (tags stripped, whitespace squished, length-capped), ready
    // for AI extraction. url is echoed back so stale results are discardable.
    void pageTextReady(const QString& url, const QString& text);
    void pageTextFailed(const QString& url, const QString& error);
    // The image re-search recovered a product URL for a blob that lacked
    // `link` (linked before the url→link capture). BagCard backfills it into
    // the bag blob so the details popup can offer the reorder link.
    void bagLinkRecovered(const QString& canonicalId, const QString& link);

    // Outcome of validateBagLink(). resolved carries the final URL the stored
    // link resolved to (following redirects) — equal to the input when nothing
    // changed, so the consumer can stamp "checked" without a rewrite; dead
    // fires only on a confirmed 404/410 so the consumer clears the link.
    void bagLinkResolved(const QString& canonicalId, const QString& link);
    void bagLinkDead(const QString& canonicalId);

private:
    void doSendCanonicalSearch(const QString& query);
    // Shared body of ensureBagImage/refreshBagImage. force=true skips the
    // cached-file short-circuit and the once-per-session attempt guard, so a
    // refresh actually re-fetches where an ensure would no-op.
    void startBagImageResolve(const QString& canonicalId, const QString& roastName,
                              const QString& productUrl, bool force);
    void fetchProductPage(const QString& canonicalId, const QString& productUrl);
    void downloadBagImage(const QString& canonicalId, const QString& imageUrl);
    QString imageCacheDir() const;

    QNetworkAccessManager* m_networkManager = nullptr;  // Non-owning
    QString m_visualizerBaseUrl;

    // Canonical (Visualizer) debounce state — coalesces type-ahead; see search().
    QTimer m_canonicalDebounceTimer;
    QString m_pendingCanonicalQuery;
    QPointer<QNetworkReply> m_activeCanonicalReply;
    QString m_activeCanonicalQuery;  // Query of the in-flight canonical reply.

    // Session cache: normalized query -> parsed entries.
    QHash<QString, QVariantList> m_canonicalCache;

    // Image cache state: directory override (tests) and the one-attempt-per-
    // session guards that keep failed resolutions from retrying every view.
    QString m_imageCacheDir;
    QSet<QString> m_imageAttempted;
    QSet<QString> m_linkAttempted;
    QSet<QString> m_linkValidated;  // validateBagLink: one GET per id per session
    // Ids whose image resolution is waiting on link recovery (legacy blobs).
    QSet<QString> m_imageAwaitingLink;
};
