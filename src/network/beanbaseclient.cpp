#include "beanbaseclient.h"
#include "beanbase_blob.h"
#include "core/beanbaselogging.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace {
// Visualizer's official canonical search API (open-source: miharekar/visualizer,
// Api::CanonicalCoffeeBagsController — unauthenticated, substring + multi-word
// matching, documented in openapi.yaml). The search response carries the
// canonical UUID we store locally AND send on shot PATCH, plus the full
// descriptive block, so one request is enough.
constexpr auto kVisualizerBaseUrl = "https://visualizer.coffee";

// Canonical search: type-ahead cadence. The endpoint is rate-limited
// (50 req/min per IP); debounce + session cache keep a single user well under it.
constexpr int kCanonicalDebounceMs = 350;

// Stalled-connection guard on every request (matches shotserver/aiprovider).
// Surfaces as OperationCanceledError; handlers distinguish our own
// supersede-abort (reply already detached) from a timeout (still active).
constexpr int kTransferTimeoutMs = 15000;

// Visualizer API column -> our blob key (the vocabulary downstream consumers —
// popup, advisor block, uploads — expect). Empty/null values are dropped by the
// caller. elevation is a single display string here.
constexpr struct { const char* apiKey; const char* blobKey; } kAttrMap[] = {
    {"roast_level", "degree"},
    {"country", "origin"},
    {"region", "region"},
    {"farmer", "producer"},
    {"variety", "variety"},
    {"processing", "process"},
    {"harvest_time", "harvest"},
    {"tasting_notes", "tastingNotes"},
    {"elevation", "elevation"},
    // The roaster's product page. Consumers: the details popup's open-page
    // button and ensureBagImage's og:image resolution.
    {"url", "link"},
};

// Internet Archive availability API. Asked only about a URL already proven
// dead, and answers the one question that matters — is there a good capture,
// and where. The CDX server returns the whole capture history and would need
// paging and filtering to say the same thing.
constexpr auto kArchiveBaseUrl = "https://archive.org";

// Result-ordering probes run in parallel but not unboundedly: a broad search
// must not open a socket per row on a tablet.
constexpr int kMaxLinkProbesInFlight = 4;

// Bag image cache limits: a product photo is typically 100 KB–2 MB; the cap
// keeps the whole cache a bounded, evictable convenience.
constexpr qint64 kBagImageMaxBytes = 8 * 1024 * 1024;
constexpr qint64 kBagImageCacheCapBytes = 30 * 1024 * 1024;
}  // namespace

BeanBaseClient::BeanBaseClient(QNetworkAccessManager* networkManager,
                               Settings* settings, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_visualizerBaseUrl(QString::fromLatin1(kVisualizerBaseUrl))
    , m_archiveBaseUrl(QString::fromLatin1(kArchiveBaseUrl))
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
        // Supersede the in-flight request. abort() emits finished() SYNCHRONOUSLY
        // (same-thread direct connection), so detach the pointer FIRST: the
        // handler then sees wasActive == false and stays silent, leaving this the
        // single terminal signal ("superseded") for the displaced query. Aborting
        // before clearing would let the handler also emit a spurious "network".
        QNetworkReply* superseded = m_activeCanonicalReply;
        m_activeCanonicalReply.clear();
        emit searchFailed(m_activeCanonicalQuery, QStringLiteral("superseded"));
        superseded->abort();
    }

    QUrl url(QStringLiteral("%1/api/canonical_coffee_bags").arg(m_visualizerBaseUrl));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(urlQuery);

    QNetworkRequest request{url};
    request.setRawHeader("Accept", "application/json");
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
            bool parsedOk = false;
            const QVariantList entries = parseCanonicalCoffeeBags(reply->readAll(), &parsedOk);
            if (!parsedOk) {
                // 200 with a non-JSON body — the API contract drifted, not a
                // missing bean. Surface as parse so the bar's copy stays honest.
                emit searchFailed(query, QStringLiteral("parse"));
                return;
            }
            m_canonicalCache.insert(query.toLower(), entries);
            emit searchResults(query, entries);
        } else {
            // Non-200 (incl. HTTP 429 rate limit) — a reach failure, not an empty
            // result; the bar must not render "No matches".
            emit searchFailed(query, QStringLiteral("network"));
        }
    });
}

void BeanBaseClient::fetchCanonicalDetails(const QVariantMap& entry) {
    const QString canonicalId = entry.value(QStringLiteral("id")).toString();
    if (canonicalId.isEmpty())
        return;

    // The /api search already carried every descriptive field on the entry, so
    // enrichment is a local re-emit with no network round-trip. Build the attrs
    // map from the descriptive blob keys; canonicalRoasterId rides along only
    // when there is descriptive data (matches the prior payload-gated emit).
    QVariantMap attrs;
    for (const char* blobKey : {"degree", "origin", "region", "producer", "variety",
                                "process", "harvest", "tastingNotes", "elevation"}) {
        const QString v = entry.value(QLatin1String(blobKey)).toString();
        if (!v.isEmpty())
            attrs.insert(QLatin1String(blobKey), v);
    }
    if (attrs.isEmpty())
        return;  // No descriptive data — nothing to enrich (the gather's grace
                 // timer covers it, as the old payload-less path did).

    const QString roasterId = entry.value(QStringLiteral("canonicalRoasterId")).toString();
    if (!roasterId.isEmpty())
        attrs.insert(QStringLiteral("canonicalRoasterId"), roasterId);

    // Deferred so the emit stays asynchronous: consumers (the MCP gather, QML
    // Connections) connect canonicalDetails before — or right after — invoking
    // this, and a synchronous emit could fire before a just-after connect.
    QPointer<BeanBaseClient> self(this);
    QMetaObject::invokeMethod(this, [self, canonicalId, attrs]() {
        if (self)
            emit self->canonicalDetails(canonicalId, attrs);
    }, Qt::QueuedConnection);
}

QString BeanBaseClient::imageCacheDir() const {
    if (!m_imageCacheDir.isEmpty())
        return m_imageCacheDir;
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/bagimages");
}

namespace {
// Magic numbers for the formats a roaster's product photo is actually in.
// Enough to tell a picture from the HTML a soft-404 serves; not a decoder.
bool looksLikeImage(const QByteArray& bytes) {
    static const QList<QByteArray> kSignatures{
        QByteArrayLiteral("\x89PNG\r\n\x1a\n"),  // PNG
        QByteArrayLiteral("\xff\xd8\xff"),          // JPEG
        QByteArrayLiteral("GIF87a"), QByteArrayLiteral("GIF89a"),
        QByteArrayLiteral("BM"),                      // BMP
    };
    for (const QByteArray& signature : kSignatures) {
        if (bytes.startsWith(signature))
            return true;
    }
    // WebP is RIFF with WEBP at offset 8 — bare "RIFF" is also WAV and AVI.
    if (bytes.size() >= 12 && bytes.startsWith(QByteArrayLiteral("RIFF"))
        && bytes.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return true;
    // SVG needs an actual <svg element. Accepting a bare "<?xml" prolog let
    // through the XML error bodies S3/GCS-backed CDNs return for a missing
    // object — the very soft-404 shape this check exists to reject.
    const QByteArray head = bytes.left(512).toLower();
    return head.contains(QByteArrayLiteral("<svg"));
}

// The canonical id doubles as the cache filename. Ids are Visualizer UUIDs,
// but the value round-trips through blobs, backups, and device migration —
// refuse anything that isn't a plain filename component so a crafted id can
// never escape the cache directory.
bool isSafeCacheFilename(const QString& id) {
    return !id.isEmpty() && !id.contains(QLatin1Char('/'))
        && !id.contains(QLatin1Char('\\')) && !id.contains(QLatin1String(".."));
}
}  // namespace

QString BeanBaseClient::bagImagePath(const QString& canonicalId) const {
    if (!isSafeCacheFilename(canonicalId))
        return {};
    const QString path = imageCacheDir() + QLatin1Char('/') + canonicalId;
    return QFile::exists(path) ? path : QString();
}

void BeanBaseClient::ensureBagImage(const QString& canonicalId,
                                    const QString& roastName,
                                    const QString& productUrl) {
    startBagImageResolve(canonicalId, roastName, productUrl, /*force=*/false);
}

void BeanBaseClient::startBagImageResolve(const QString& canonicalId,
                                          const QString& roastName,
                                          const QString& productUrl,
                                          bool force) {
    if (!isSafeCacheFilename(canonicalId)) {
        // A corrupt canonical id, not an expected miss — the caller believes
        // this bag has a photo and will wait forever for a signal that is
        // never coming, so say so rather than bailing mutely.
        BEANBASE_WARN_STDERR("Image", QStringLiteral("Refusing unsafe cache key %1").arg(canonicalId));
        return;
    }

    const QString existing = force ? QString() : bagImagePath(canonicalId);
    if (!existing.isEmpty()) {
        // Deferred re-emit so a consumer that connects right after invoking
        // still hears it (same rationale as fetchCanonicalDetails).
        QPointer<BeanBaseClient> self(this);
        QMetaObject::invokeMethod(this, [self, canonicalId, existing]() {
            if (self)
                emit self->bagImageReady(canonicalId, existing);
        }, Qt::QueuedConnection);
        return;
    }

    if (!force && m_imageAttempted.contains(canonicalId))
        return;
    m_imageAttempted.insert(canonicalId);

    if (!productUrl.isEmpty()) {
        fetchProductPage(canonicalId, productUrl);
        return;
    }

    // Manual bags (add-bag-detail-editing) use a "bag-<rowid>" cache key —
    // there is no canonical entry to recover a URL from, so no URL means no
    // image, full stop.
    if (canonicalId.startsWith(QLatin1String("bag-")))
        return;

    // Legacy blob without `link` (captured only since the url→link mapping):
    // recover the product URL first, then continue the image chain from it.
    m_imageAwaitingLink.insert(canonicalId);
    recoverBagLink(canonicalId, roastName);
}

void BeanBaseClient::replaceBagImageFromUrl(const QString& imageKey, const QString& imageUrl) {
    if (!isSafeCacheFilename(imageKey) || imageUrl.trimmed().isEmpty())
        return;
    // No cache-hit short-circuit: replacing the entry is the point. The attempt
    // guard is re-armed so a failed download doesn't retry all session.
    m_imageAttempted.insert(imageKey);
    downloadBagImage(imageKey, imageUrl.trimmed());
}

void BeanBaseClient::cacheBagImageFromUrl(const QString& imageKey, const QString& imageUrl) {
    // Stage-2 extraction returned the product photo's URL directly (SPA pages
    // have no og:image for fetchProductPage to find) — download it into the
    // cache under the key, exactly like an og:image hit. Cache hits win; the
    // attempt guard is set so a failed download doesn't retry all session.
    if (!isSafeCacheFilename(imageKey) || imageUrl.trimmed().isEmpty())
        return;
    if (!bagImagePath(imageKey).isEmpty())
        return;
    m_imageAttempted.insert(imageKey);
    downloadBagImage(imageKey, imageUrl.trimmed());
}

void BeanBaseClient::refreshBagImage(const QString& canonicalId,
                                     const QString& roastName,
                                     const QString& productUrl) {
    // The product URL was user-edited (add-bag-detail-editing): the cached
    // pixels and the once-per-session attempt guard both describe the OLD
    // page. Clear the guard and force a re-resolve; downloadBagImage's atomic
    // rename replaces the file only once the new bytes are on disk, so the bag
    // is never photo-less in between and a failed refresh leaves what was
    // already there instead of a permanent placeholder.
    m_imageAttempted.remove(canonicalId);
    startBagImageResolve(canonicalId, roastName, productUrl, /*force=*/true);
}

void BeanBaseClient::recoverBagLink(const QString& canonicalId, const QString& roastName) {
    if (!isSafeCacheFilename(canonicalId))
        return;
    if (m_linkAttempted.contains(canonicalId))
        return;
    m_linkAttempted.insert(canonicalId);

    const QString query = roastName.trimmed();
    if (query.isEmpty())
        return;

    QUrl url(QStringLiteral("%1/api/canonical_coffee_bags").arg(m_visualizerBaseUrl));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(urlQuery);
    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, canonicalId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QVariantList entries = parseCanonicalCoffeeBags(reply->readAll());
        for (const QVariant& v : entries) {
            const QVariantMap entry = v.toMap();
            if (entry.value(QStringLiteral("id")).toString() != canonicalId)
                continue;
            const QString link = entry.value(QStringLiteral("link")).toString();
            if (!link.isEmpty()) {
                // Announce the recovered product URL so consumers can backfill
                // it into blobs that predate the url→link capture (BagCard
                // persists it; the details popup shows it for reordering).
                emit bagLinkRecovered(canonicalId, link);
                // Continue a pending image resolution that was waiting on it.
                if (m_imageAwaitingLink.remove(canonicalId))
                    fetchProductPage(canonicalId, link);
            }
            return;
        }
    });
}

void BeanBaseClient::validateBagLink(const QString& canonicalId, const QString& productUrl) {
    if (!isSafeCacheFilename(canonicalId))
        return;
    // Recovery is terminal. Validating a snapshot URL would churn the stored
    // link (the archive redirects to a neighbouring timestamp) and, on a
    // snapshot 404, leave the bag re-probing every session forever.
    if (isArchiveUrl(productUrl))
        return;
    if (m_linkValidated.contains(linkGuardKey(canonicalId, productUrl)))
        return;
    const QUrl url(productUrl);
    if (!url.isValid() || !url.scheme().startsWith(QLatin1String("http")))
        return;
    m_linkValidated.insert(linkGuardKey(canonicalId, productUrl));

    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);
    // Follow redirects so we learn the roaster's current canonical URL — a
    // renamed/aliased Shopify handle 301s to the live one.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, canonicalId, productUrl]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Confirmed gone → ask the archive before giving up. Only a confirmed
        // no-capture clears the link; an archive fault emits nothing, exactly
        // like a transient roaster failure (timeout, DNS, 5xx — status 0 or
        // ≥500), so a later session retries rather than losing a link to a blip.
        if (status == 404 || status == 410) {
            queryArchiveSnapshot(canonicalId, productUrl,
                                 [this, canonicalId](const QString& snapshot, bool answered) {
                                     if (!snapshot.isEmpty())
                                         emit bagLinkArchived(canonicalId, snapshot);
                                     else if (answered)
                                         emit bagLinkDead(canonicalId);
                                 });
            return;
        }
        if (reply->error() != QNetworkReply::NoError)
            return;
        // Resolved (possibly via redirect). Emit even when unchanged so the
        // consumer can mark the bag checked without rewriting; when the final
        // URL differs, the stale alias is normalized to the durable one.
        const QString resolved = reply->url().toString();
        emit bagLinkResolved(canonicalId, resolved.isEmpty() ? productUrl : resolved);
    });
}

void BeanBaseClient::fetchProductPage(const QString& canonicalId, const QString& productUrl) {
    // A snapshot is fetched in its `id_` form, which serves the ORIGINAL page
    // bytes: no archive toolbar, no URL rewriting, so og:image already names
    // the roaster's own asset instead of an `im_` proxy — the preference for
    // the original falls out of the fetch rather than needing logic here.
    // Measured on one capture: 56 KB against 304 KB for the rewritten form.
    const QString fetchUrl = archiveRawForm(productUrl);
    const QUrl url(fetchUrl);
    if (!url.isValid() || !url.scheme().startsWith(QLatin1String("http")))
        return;
    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, canonicalId, productUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QString imageUrl = extractOgImage(reply->readAll());
        if (imageUrl.isEmpty())
            return;
        // Roaster asset hosts routinely outlive the product pages that named
        // them — the original is the higher-fidelity, lower-latency source, so
        // it is tried first. Only when it is gone does the archive's own copy
        // of the same asset stand in.
        const QString archivedAsset = archiveAssetForm(productUrl, imageUrl);
        downloadBagImage(canonicalId, imageUrl, archivedAsset);
    });
}

void BeanBaseClient::downloadBagImage(const QString& canonicalId, const QString& imageUrl,
                                     const QString& fallbackImageUrl) {
    const QUrl url(imageUrl);
    if (!url.isValid() || !url.scheme().startsWith(QLatin1String("http")))
        return;
    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, canonicalId, fallbackImageUrl]() {
        reply->deleteLater();
        const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                                         : QByteArray();
        // A parked domain or a CDN soft-404 answers 200 with an HTML body. Left
        // unchecked those bytes become the bag's photo, and because the file
        // then EXISTS the resolution never runs again — on disk, across
        // restarts. fetchPageText already checks its content type; this is the
        // path that did not.
        //
        // Either evidence suffices: the header, or the bytes themselves. A CDN
        // that mislabels a real photo (application/octet-stream is common) must
        // not lose the bag its picture. The converse is NOT covered — a server
        // that labels an error page image/jpeg still gets cached — because
        // requiring both would drop legitimate photos from the mislabelling
        // CDNs, which are far commoner than a lying one.
        const QString contentType =
            reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const bool usable = !bytes.isEmpty() && bytes.size() <= kBagImageMaxBytes
                            && (contentType.startsWith(QLatin1String("image/"))
                                || looksLikeImage(bytes));
        if (!usable) {
            // Every unusable answer, not just a transport error, is what the
            // archived copy exists to rescue.
            if (!fallbackImageUrl.isEmpty()) {
                downloadBagImage(canonicalId, fallbackImageUrl);
                return;
            }
            // An oversized or empty body is an ordinary, expected miss and stays
            // silent like every other. Bytes that arrived fine and simply are
            // not a picture mean the URL points at something else — worth
            // saying, because the bag will show a placeholder forever.
            if (reply->error() == QNetworkReply::NoError && !bytes.isEmpty()
                && bytes.size() <= kBagImageMaxBytes)
                BEANBASE_WARN_STDERR("Image", QStringLiteral("Refusing unusable image: %1, %2 bytes")
                                                  .arg(contentType).arg(bytes.size()));
            return;
        }

        // File write + eviction off the main thread (disk-I/O rule). The write
        // is atomic (QSaveFile: temp file, verified write, commit-or-discard) so
        // a disk-full or crash mid-write can never leave a truncated file that
        // satisfies bagImagePath() and suppresses re-resolution forever. The
        // completion hops back via the `this` connection context and emits when
        // a file is present at the path — after a refresh whose download failed
        // that is the PREVIOUS photo, which is the intended outcome: the bag
        // keeps the picture it had rather than losing it to a failed refresh.
        const QString dir = imageCacheDir();
        const QString path = dir + QLatin1Char('/') + canonicalId;
        QPointer<BeanBaseClient> self(this);
        QThread* worker = QThread::create([bytes, dir, path]() {
            if (!QDir().mkpath(dir)) {
                BEANBASE_WARN_STDERR("Image", QStringLiteral("Cannot create cache dir %1").arg(dir));
                return;
            }
            // QSaveFile, not QFile+rename: it commits atomically OVER an
            // existing target. QFile::rename refuses a destination that already
            // exists, which a refresh always has now that the old photo is kept
            // until the new bytes land — so every refresh would "fail" its
            // write and silently keep serving the stale image.
            QSaveFile f(path);
            const bool ok = f.open(QIODevice::WriteOnly)
                && f.write(bytes) == bytes.size()
                && f.commit();
            if (!ok) {
                // A local disk fault (full disk, permissions) — unlike the
                // expected network/og:image misses, this is worth a log line.
                // QSaveFile discards its own temp file on a failed commit.
                BEANBASE_WARN_STDERR("Image", QStringLiteral("Write failed for %1 - %2")
                                                  .arg(path, f.errorString()));
                return;
            }
            // Keep the cache a cache: evict oldest-written files beyond the
            // cap (Time|Reversed = oldest first), never the one just written.
            // A concurrent worker for another id could evict this file between
            // the emit and the QML load — cosmetic and self-healing (next
            // session re-resolves), so not worth serializing.
            QFileInfoList files = QDir(dir).entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
            qint64 total = 0;
            for (const QFileInfo& fi : files)
                total += fi.size();
            for (const QFileInfo& fi : files) {
                if (total <= kBagImageCacheCapBytes)
                    break;
                if (fi.filePath() == path)
                    continue;
                if (!QFile::remove(fi.filePath())) {
                    BEANBASE_WARN_STDERR("Image", QStringLiteral("Eviction failed for %1").arg(fi.filePath()));
                    continue;  // Don't credit the failed removal against the cap.
                }
                total -= fi.size();
            }
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &QThread::finished, this, [self, canonicalId, path]() {
            if (self && QFile::exists(path))
                emit self->bagImageReady(canonicalId, path);
        });
        worker->start();
    });
}

QString BeanBaseClient::mergeBeanDetails(const QString& blob, const QVariantMap& edits) {
    return BeanBaseBlob::mergeBeanDetails(blob, edits);
}

QVariantMap BeanBaseClient::applyExtraction(const QString& blob, const QVariantMap& extracted,
                                            const QVariantMap& current) {
    const auto out = BeanBaseBlob::applyExtraction(blob, extracted, current);
    return {{QStringLiteral("blob"), out.blob},
            {QStringLiteral("applied"), out.applied},
            {QStringLiteral("corrections"), out.corrections}};
}

bool BeanBaseClient::linkIsUsable(const QString& blob, const QString& link) {
    return BeanBaseBlob::linkIsUsable(blob, link);
}

QString BeanBaseClient::blobWithLink(const QString& blob, const QString& link) {
    return BeanBaseBlob::blobWithLink(blob, link);
}

QString BeanBaseClient::blobWithLinkVerdict(const QString& blob, const QString& link, bool dead) {
    return BeanBaseBlob::blobWithLinkVerdict(blob, link, dead);
}

QString BeanBaseClient::revertToCanonical(const QString& blob) {
    return BeanBaseBlob::revertToCanonical(blob);
}

bool BeanBaseClient::blobDiffersFromCanonical(const QString& blob) {
    return BeanBaseBlob::differsFromCanonical(blob);
}

void BeanBaseClient::fetchPageText(const QString& url) {
    // Failure reasons: stable codes ("invalidUrl", "notAWebPage", "emptyPage")
    // the QML layer translates; transport failures pass Qt's errorString.
    const QUrl parsed(url);
    if (!parsed.isValid() || !parsed.scheme().startsWith(QLatin1String("http"))) {
        // The http(s)-only gate matters: the URL is user-entered and the text
        // goes to a third-party AI provider — a file:// URL must never be read.
        BEANBASE_WARN_STDERR("Extract", QStringLiteral("Rejected non-http url %1").arg(url));
        QPointer<BeanBaseClient> self(this);
        QMetaObject::invokeMethod(this, [self, url]() {
            if (self)
                emit self->pageTextFailed(url, QStringLiteral("invalidUrl"));
        }, Qt::QueuedConnection);
        return;
    }
    // An archive snapshot is read in its `id_` form: the original page bytes,
    // free of the archive's own toolbar markup, which would otherwise reach the
    // AI as page text. `url` is still what is echoed back on the signals, so
    // the caller matches on the URL it asked for.
    //
    // A URL that is already a snapshot has nowhere further to fall back to, so
    // it asks the archive nothing.
    requestPageText(archiveRawForm(url), url, !isArchiveUrl(url));
}

// static
bool BeanBaseClient::archiveRetryApplies(int httpStatus, bool archiveFallback) {
    // Only 404/410 asks the archive. Every other failure — a timeout, a DNS
    // error, a 5xx, a 403 — leaves the page's own error standing: none of them
    // says the page is DELISTED, and answering them from a years-old snapshot
    // would hide a broken connection behind stale content.
    return archiveFallback && (httpStatus == 404 || httpStatus == 410);
}

void BeanBaseClient::requestPageText(const QString& fetchUrl, const QString& reportUrl,
                                     bool archiveFallback) {
    QNetworkRequest request{QUrl(fetchUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, reportUrl, archiveFallback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const QString pageError = reply->errorString();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (archiveRetryApplies(status, archiveFallback)) {
                fetchArchiveAvailability(
                    reportUrl, [this, reportUrl, pageError](const QString& snapshot, bool answered) {
                        if (!snapshot.isEmpty()) {
                            // reportUrl, not the snapshot: the caller gates
                            // completion on the URL it sent. `false`: a snapshot
                            // that is itself gone ends the line, never a chain.
                            requestPageText(archiveRawForm(snapshot), reportUrl, false);
                            return;
                        }
                        // The USER is told about the page either way: nothing
                        // here marks a bag dead, so the fault/miss distinction
                        // carries no consequence for them, and naming the
                        // archive would point them at a service they never
                        // asked about. The LOG keeps it, because "no capture"
                        // and "the archive refused to answer" send a reader to
                        // different places — a rate limit succeeds on retry.
                        BEANBASE_WARN_STDERR("Extract",
                            QStringLiteral("%1 unreadable, %2 - %3")
                                .arg(reportUrl,
                                     answered ? QStringLiteral("no archived copy")
                                              : QStringLiteral("archive gave no answer"),
                                     pageError));
                        emit pageTextFailed(reportUrl, pageError);
                    });
                return;
            }
            BEANBASE_WARN_STDERR("Extract", QStringLiteral("%1 unreadable - %2").arg(reportUrl, pageError));
            emit pageTextFailed(reportUrl, pageError);
            return;
        }
        // A PDF/image/zip URL would decode to replacement-character soup that
        // passes the length gate and earns a confident "nothing found on the
        // page" — reject non-text content as a FORMAT failure instead.
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (!contentType.isEmpty()
            && !contentType.startsWith(QLatin1String("text/"))
            && !contentType.contains(QLatin1String("html"), Qt::CaseInsensitive)
            && !contentType.contains(QLatin1String("xml"), Qt::CaseInsensitive)) {
            BEANBASE_WARN_STDERR("Extract", QStringLiteral("%1 is not a web page (%2)")
                                                .arg(reportUrl, contentType));
            emit pageTextFailed(reportUrl, QStringLiteral("notAWebPage"));
            return;
        }
        // The 15 s transfer timeout bounds time, not bytes — cap the body so
        // a huge asset can't stall the main thread in the regex passes.
        QByteArray body = reply->readAll();
        constexpr qsizetype kMaxBodyBytes = 512 * 1024;
        if (body.size() > kMaxBodyBytes)
            body.truncate(kMaxBodyBytes);
        const QString text = extractPageText(body);
        // Visualizer treats < 100 chars as "blocked or empty" and falls back
        // to a scraping proxy; we have no proxy, so it is simply a failure.
        if (text.size() < 100) {
            BEANBASE_WARN_STDERR("Extract", QStringLiteral("%1 yielded no readable text").arg(reportUrl));
            emit pageTextFailed(reportUrl, QStringLiteral("emptyPage"));
            return;
        }
        emit pageTextReady(reportUrl, text);
    });
}

// static
QString BeanBaseClient::extractPageText(const QByteArray& html) {
    QString text = QString::fromUtf8(html);
    // Element bodies that are never prose, then every remaining tag — the
    // same reduction Visualizer's scraper applies before AI extraction.
    static const QRegularExpression kBlockRe(
        QStringLiteral("<(script|style|svg)\\b[^>]*>.*?</\\1\\s*>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression kTagRe(QStringLiteral("<[^>]+>"));
    static const QRegularExpression kSpaceRe(QStringLiteral("\\s+"));
    text.remove(kBlockRe);
    text.replace(kTagRe, QStringLiteral(" "));
    // The handful of entities that actually occur in shop prose.
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    text = text.replace(kSpaceRe, QStringLiteral(" ")).trimmed();
    // Cap what we hand to the model. 48k (~12k tokens) — the old 20k cap
    // truncated from the WRONG end on menu-heavy Shopify pages: Yunnan
    // Sourcing's product page squishes to 21.7k chars with ~14.8k of nav
    // cruft first, leaving the actual product description at 14.8k–17.5k
    // (verified add-recipe-wizard-tea). The tail past the description is
    // reviews/footer noise, safe to lose.
    constexpr qsizetype kMaxChars = 48000;
    if (text.size() > kMaxChars)
        text.truncate(kMaxChars);
    return text;
}

// A Wayback snapshot URL: scheme://<host>/web/<timestamp>[<modifier>]/<original
// url>. The modifier (id_, im_, if_, cs_, js_ …) selects what the archive
// serves; with none it rewrites the page for browsing.
namespace {
QString g_archiveSnapshotHost = QStringLiteral("web.archive.org");

QRegularExpression archiveSnapshotRe(const QString& host) {
    return QRegularExpression(
        QStringLiteral("^(https?://%1/web/)(\\d{4,14})([a-z]{2}_)?/(.+)$")
            .arg(QRegularExpression::escape(host)),
        QRegularExpression::CaseInsensitiveOption);
}
}  // namespace

QString BeanBaseClient::archiveSnapshotHost() { return g_archiveSnapshotHost; }
void BeanBaseClient::setArchiveSnapshotHost(const QString& host) { g_archiveSnapshotHost = host; }

bool BeanBaseClient::isArchiveUrl(const QString& url, const QString& host) {
    return archiveSnapshotRe(host).match(url.trimmed()).hasMatch();
}

QString BeanBaseClient::archiveRawForm(const QString& snapshotUrl, const QString& host) {
    const auto m = archiveSnapshotRe(host).match(snapshotUrl.trimmed());
    if (!m.hasMatch())
        return snapshotUrl;
    // Already carries a modifier — leave it. Re-applying would produce
    // `…<ts>im_id_/…`, which the archive does not serve.
    if (!m.captured(3).isEmpty())
        return snapshotUrl;
    return m.captured(1) + m.captured(2) + QStringLiteral("id_/") + m.captured(4);
}

QString BeanBaseClient::archiveAssetForm(const QString& snapshotUrl, const QString& assetUrl,
                                        const QString& host) {
    const auto m = archiveSnapshotRe(host).match(snapshotUrl.trimmed());
    if (!m.hasMatch() || assetUrl.trimmed().isEmpty())
        return {};
    // A capture whose og:image was already absolute-to-the-archive needs no
    // wrapping; doing it anyway yields `…im_/https://web.archive.org/…im_/…`.
    if (isArchiveUrl(assetUrl, host))
        return assetUrl.trimmed();
    return m.captured(1) + m.captured(2) + QStringLiteral("im_/") + assetUrl.trimmed();
}

QString BeanBaseClient::parseArchiveSnapshot(const QByteArray& json, bool* ok) {
    auto fault = [ok]() {
        if (ok)
            *ok = false;
        return QString();
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fault();

    // `archived_snapshots` is the availability API's own envelope, and its
    // presence is what makes this an ANSWER rather than merely some JSON. Well-
    // formedness alone is not enough: an archive error body ({"error": …}), a
    // proxy interstitial, or a renamed envelope in a future API version would
    // all parse — and a wrong "no capture" verdict permanently clears a bag's
    // only remaining URL, which nothing can re-derive.
    const QJsonValue snapshots = doc.object().value(QStringLiteral("archived_snapshots"));
    if (!snapshots.isObject())
        return fault();
    if (ok)
        *ok = true;

    const QJsonObject closest = snapshots.toObject().value(QStringLiteral("closest")).toObject();
    if (!closest.value(QStringLiteral("available")).toBool())
        return {};
    // status is a string in this API ("200"), not a number.
    if (closest.value(QStringLiteral("status")).toString() != QLatin1String("200"))
        return {};

    QString url = closest.value(QStringLiteral("url")).toString().trimmed();
    if (!isArchiveUrl(url)) {
        // The archive has just SAID a good capture exists. Failing our own
        // parse of its URL means this code is out of date, not that the page is
        // unarchived — and reporting a miss here would stamp dead a bag whose
        // capture demonstrably exists.
        BEANBASE_WARN_STDERR("Archive", QStringLiteral("Availability API reported a capture at an unparseable URL %1").arg(url));
        return fault();
    }
    // The API answers in http even for an https capture; the app never stores
    // an http URL it could have stored as https.
    if (url.startsWith(QLatin1String("http://")))
        url.replace(0, 4, QStringLiteral("https"));
    return url;
}

void BeanBaseClient::fetchArchiveAvailability(const QString& productUrl,
                                             std::function<void(const QString&, bool)> done) {
    QUrl url(QStringLiteral("%1/wayback/available").arg(m_archiveBaseUrl));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("url"), productUrl);
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, false);  // Archive fault — never a "no capture" verdict.
            return;
        }
        bool wellFormed = false;
        const QString snapshot = parseArchiveSnapshot(reply->readAll(), &wellFormed);
        done(snapshot, wellFormed);
    });
}

void BeanBaseClient::queryArchiveSnapshot(const QString& canonicalId, const QString& productUrl,
                                          std::function<void(const QString&, bool)> done) {
    const QString target = productUrl.trimmed();
    const QUrl parsed(target);
    // Not askable: no answer, and no verdict either — the caller must not read
    // an unasked question as "no capture".
    if (!isSafeCacheFilename(canonicalId) || !parsed.isValid()
        || !parsed.scheme().startsWith(QLatin1String("http")) || isArchiveUrl(target)
        || m_archiveAttempted.contains(linkGuardKey(canonicalId, target))) {
        done({}, false);
        return;
    }
    m_archiveAttempted.insert(linkGuardKey(canonicalId, target));
    fetchArchiveAvailability(target, std::move(done));
}

void BeanBaseClient::lookupArchivedLink(const QString& canonicalId, const QString& productUrl) {
    queryArchiveSnapshot(canonicalId, productUrl,
                         [this, canonicalId](const QString& snapshot, bool /*answered*/) {
                             if (!snapshot.isEmpty())
                                 emit bagLinkArchived(canonicalId, snapshot);
                         });
}

// --- Per-URL link state, for ordering search results ---
//
// A HEAD is enough to tell a live product page from a gone one, and costs a
// fraction of the page. Some storefronts answer HEAD with 405/501, so those
// two retry once as a GET rather than being reported as dead.

QString BeanBaseClient::linkState(const QString& url) const {
    const QString key = url.trimmed();
    if (key.isEmpty())
        return QStringLiteral("none");
    if (isArchiveUrl(key))
        return QStringLiteral("archived");
    return m_linkStateByUrl.value(key, QStringLiteral("unknown"));
}

void BeanBaseClient::cancelQueuedLinkProbes() {
    // Queued probes belong to the query that enqueued them. Left in place they
    // drain ahead of the rows the user is now looking at — head-of-line
    // blocking on exactly the data the ordering needs first. In-flight requests
    // are left alone; their answers are still cached and still useful.
    //
    // Every dropped entry still ANSWERS. The queue is shared with the bag
    // editor's suggestion probe, which waits on linkStateResolved before it can
    // offer a page the user has already paid to find; dropping it silently
    // stranded exactly the case this signal's every-probe-answers contract
    // exists to prevent.
    const QStringList dropped = std::exchange(m_linkStateQueued, {});
    for (const QString& url : dropped)
        emit linkStateResolved(url, QStringLiteral("unknown"));
}

void BeanBaseClient::probeLinkState(const QString& url) {
    const QString key = url.trimmed();
    const QUrl parsed(key);
    if (key.isEmpty() || !parsed.isValid() || !parsed.scheme().startsWith(QLatin1String("http")))
        return;
    // An archive URL is already its own answer, and a state we know is a state
    // we keep — the session cache is what stops a repeated search re-probing.
    if (isArchiveUrl(key) || m_linkStateByUrl.contains(key) || m_linkStateInFlight.contains(key)
        || m_linkStateQueued.contains(key))
        return;
    if (m_linkStateUnresolvable.contains(key)) {
        // Asked once already, no verdict. Do not ask again — but DO answer, or
        // a caller waiting on the signal (the suggestion probe) waits forever
        // for a request that was never going to be sent. Deferred so a consumer
        // that connects right after invoking this still hears it.
        QPointer<BeanBaseClient> self(this);
        QMetaObject::invokeMethod(this, [self, key]() {
            if (self)
                emit self->linkStateResolved(key, QStringLiteral("unknown"));
        }, Qt::QueuedConnection);
        return;
    }

    if (m_linkStateInFlight.size() >= kMaxLinkProbesInFlight) {
        m_linkStateQueued.append(key);
        return;
    }
    startLinkStateProbe(key, /*useGet=*/false);
}

void BeanBaseClient::startLinkStateProbe(const QString& url, bool useGet) {
    m_linkStateInFlight.insert(url);

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kTransferTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = useGet ? m_networkManager->get(request) : m_networkManager->head(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, useGet]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // A storefront that refuses HEAD is not a dead page.
        if (!useGet && (status == 405 || status == 501)) {
            m_linkStateInFlight.remove(url);
            startLinkStateProbe(url, /*useGet=*/true);
            return;
        }
        if (status == 404 || status == 410) {
            fetchArchiveAvailability(url, [this, url](const QString& snapshot, bool answered) {
                // An unanswered archive leaves the state unknown rather than
                // claiming the entry has nothing — a row is never labelled
                // worse than what is actually known about it.
                if (!snapshot.isEmpty())
                    finishLinkStateProbe(url, QStringLiteral("archived"));
                else if (answered)
                    finishLinkStateProbe(url, QStringLiteral("none"));
                else
                    finishLinkStateProbe(url, QString());
            });
            return;
        }
        // Anything else — including a transient failure — leaves the entry
        // ordered as live, which is where an unresolved row already sits.
        finishLinkStateProbe(url, reply->error() == QNetworkReply::NoError
                                      ? QStringLiteral("live") : QString());
    });
}

void BeanBaseClient::finishLinkStateProbe(const QString& url, const QString& state) {
    m_linkStateInFlight.remove(url);
    if (!state.isEmpty()) {
        m_linkStateByUrl.insert(url, state);
    } else {
        // Unresolvable stays "unknown" to every reader — but remember that we
        // asked, or an offline device re-probes every row on every rebuild, and
        // rebuild runs per lane arrival and per debounced keystroke.
        m_linkStateUnresolvable.insert(url);
    }
    // EVERY probe answers, including the one with no verdict. A consumer that
    // waits for a specific state otherwise waits forever — which is exactly
    // what stranded an AI-suggested page the user had already paid for,
    // silently, when its probe came back inconclusive.
    emit linkStateResolved(url, state.isEmpty() ? QStringLiteral("unknown") : state);
    while (!m_linkStateQueued.isEmpty() && m_linkStateInFlight.size() < kMaxLinkProbesInFlight) {
        const QString next = m_linkStateQueued.takeFirst();
        if (!m_linkStateByUrl.contains(next) && !m_linkStateInFlight.contains(next))
            startLinkStateProbe(next, /*useGet=*/false);
    }
}

QString BeanBaseClient::extractOgImage(const QByteArray& html) {
    const QString text = QString::fromUtf8(html);
    // Any <meta …> tag declaring og:image (property= or name=, secure_url
    // variant), in either attribute order; take its content attribute.
    static const QRegularExpression kTagRe(
        QStringLiteral("<meta\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kPropRe(
        QStringLiteral("(?:property|name)\\s*=\\s*[\"']og:image(?::secure_url)?[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kContentRe(
        QStringLiteral("content\\s*=\\s*[\"']([^\"']+)[\"']"),
        QRegularExpression::CaseInsensitiveOption);

    auto it = kTagRe.globalMatch(text);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        if (!kPropRe.match(tag).hasMatch())
            continue;
        const auto content = kContentRe.match(tag);
        if (!content.hasMatch())
            continue;
        const QString url = content.captured(1).trimmed();
        if (url.startsWith(QLatin1String("//")))
            return QStringLiteral("https:") + url;  // Protocol-relative.
        if (url.startsWith(QLatin1String("http")))
            return url;
    }
    return {};
}

QVariantList BeanBaseClient::parseCanonicalCoffeeBags(const QByteArray& json, bool* parsedOk) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    const bool ok = (parseError.error == QJsonParseError::NoError && doc.isObject());
    if (parsedOk)
        *parsedOk = ok;
    if (!ok)
        return {};

    QVariantList out;
    const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue& value : data) {
        const QJsonObject bag = value.toObject();
        const QString id = bag.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        QVariantMap entry;
        entry[QStringLiteral("id")] = id;
        entry[QStringLiteral("visualizerCanonicalId")] = id;
        entry[QStringLiteral("source")] = QStringLiteral("visualizer");

        const QString roaster = bag.value(QStringLiteral("canonical_roaster_name")).toString();
        if (!roaster.isEmpty())
            entry[QStringLiteral("roasterName")] = roaster;
        const QString name = bag.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            entry[QStringLiteral("roastName")] = name;
        const QString roasterId = bag.value(QStringLiteral("canonical_roaster_id")).toString();
        if (!roasterId.isEmpty())
            entry[QStringLiteral("canonicalRoasterId")] = roasterId;

        for (const auto& m : kAttrMap) {
            const QString v = bag.value(QLatin1String(m.apiKey)).toString();  // null -> ""
            if (!v.isEmpty())
                entry[QLatin1String(m.blobKey)] = v;
        }
        out.append(entry);
    }
    return out;
}
