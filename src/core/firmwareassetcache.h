#pragma once

#include <cstdint>
#include <optional>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include "core/firmwareheader.h"

// Downloads, caches, and validates the DE1 bootfwupdate.dat firmware
// binary from Decent's update CDN (fast.decentespresso.com), the same
// host Tcl de1app uses. Two channels are exposed: Stable (de1plus) and
// Nightly (de1nightly). Decent's "de1beta" channel is not wired because
// Decent has stopped updating it reliably.
//
// A sidecar .meta.json file tracks the server ETag plus the version we
// parsed out of the header, so subsequent checks can answer "is there
// something new?" with a single HEAD + If-None-Match request.
//
// The pure helpers (MetaJson serialize/parse, rangeHeaderFor) are inline
// in this header and tested in tst_firmwareassetcachehelpers.cpp. The
// FirmwareAssetCache class itself is tested at the integration level
// (see tests/tst_firmwareflow.cpp).

namespace DE1::Firmware {

// Sidecar persistence record. Lives at cachedPath() + ".meta.json".
struct MetaJson {
    QString  etag;                 // server ETag of the last-observed remote file
    uint32_t version = 0;          // Version field parsed from the cached header
    qint64   downloadedAtEpoch = 0; // wall-clock seconds since epoch
};

// JSON encode / decode. parseMeta returns std::nullopt on malformed
// input or wrong field types (e.g. a string where a number is required).
inline QByteArray serializeMeta(const MetaJson& meta) {
    QJsonObject obj;
    obj.insert(QStringLiteral("etag"),              meta.etag);
    obj.insert(QStringLiteral("version"),           static_cast<qint64>(meta.version));
    obj.insert(QStringLiteral("downloadedAtEpoch"), meta.downloadedAtEpoch);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

inline std::optional<MetaJson> parseMeta(const QByteArray& json) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    QJsonObject obj = doc.object();

    // etag is optional (empty on first write before we've seen the server)
    // but must be a string when present.
    QJsonValue etagV = obj.value(QStringLiteral("etag"));
    if (!etagV.isUndefined() && !etagV.isNull() && !etagV.isString()) {
        return std::nullopt;
    }

    // version must be a number.
    QJsonValue versionV = obj.value(QStringLiteral("version"));
    if (!versionV.isDouble()) {
        return std::nullopt;
    }

    // downloadedAtEpoch must be a number.
    QJsonValue atV = obj.value(QStringLiteral("downloadedAtEpoch"));
    if (!atV.isDouble()) {
        return std::nullopt;
    }

    MetaJson meta;
    meta.etag              = etagV.isString() ? etagV.toString() : QString();
    meta.version           = static_cast<uint32_t>(versionV.toVariant().toLongLong());
    meta.downloadedAtEpoch = atV.toVariant().toLongLong();
    return meta;
}

// Compute the HTTP Range header to resume a partial download. Returns
// std::nullopt when a Range isn't appropriate (empty cache, full cache,
// or cache already larger than server's expected total — which means
// the cache is stale or corrupt and must be wiped).
//
// `expectedTotal < 0` means "server total unknown" — we still want to
// resume from whatever's already on disk.
inline std::optional<QByteArray> rangeHeaderFor(qint64 existingSize, qint64 expectedTotal) {
    if (existingSize <= 0) {
        return std::nullopt;
    }
    if (expectedTotal >= 0 && existingSize >= expectedTotal) {
        return std::nullopt;
    }
    return QByteArray("bytes=") + QByteArray::number(existingSize) + "-";
}

}  // namespace DE1::Firmware

// -----------------------------------------------------------------------
// FirmwareAssetCache — network-driven downloader + cache.
//
// Owns the two network round-trips: (a) cheap HEAD with If-None-Match to
// detect new firmware, promoted to a 64-byte Range-GET when the ETag has
// changed so we can read the remote Version without pulling ~453 KB; and
// (b) full-body GET (optionally resumed via Range) when the user taps
// Update. All HTTP calls go through QNetworkAccessManager; setNetworkManager
// allows test scaffolding to inject a mock.
//
// Unit tests cover only the pure helpers above. The class itself is
// exercised by the §5 integration test (tst_firmwareflow.cpp) which uses
// a mocked QNetworkAccessManager to drive the full flow end-to-end.
// -----------------------------------------------------------------------

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace DE1::Firmware {

class FirmwareAssetCache : public QObject {
    Q_OBJECT

public:
    // Release channel on fast.decentespresso.com. Mirrors the channel
    // Tcl de1app selects via its app_updates_beta_enabled setting
    // (Stable = de1plus, Nightly = de1nightly).
    enum class Channel {
        Stable  = 0,
        Nightly = 1,
    };
    Q_ENUM(Channel)

    // Upstream URLs for each channel. Kept here so a future change is a
    // one-line edit and so the active URL can be inspected from tests.
    static constexpr const char* FIRMWARE_URL_STABLE =
        "https://fast.decentespresso.com/download/sync/de1plus/fw/bootfwupdate.dat";
    static constexpr const char* FIRMWARE_URL_NIGHTLY =
        "https://fast.decentespresso.com/download/sync/de1nightly/fw/bootfwupdate.dat";

    struct CheckResult {
        // Newer: remote > installed (upgrade offered).
        // Older: remote < installed (downgrade offered — matches de1app).
        // Same:  remote == installed (nothing to do).
        // Error: network/parse failure; errorDetail populated.
        enum Kind { Newer, Same, Older, Error };
        Kind     kind          = Error;
        uint32_t remoteVersion = 0;
        QString  errorDetail;                // empty on success
    };

    explicit FirmwareAssetCache(QObject* parent = nullptr);
    ~FirmwareAssetCache() override;

    // Dependency injection for tests. Pass a custom QNetworkAccessManager
    // (typically one that returns synthetic replies). FirmwareAssetCache
    // does NOT take ownership; the caller is responsible for the manager's
    // lifetime. If never called, the cache creates and owns its own manager.
    void setNetworkManager(QNetworkAccessManager* manager);

    // Override the cache root (defaults to
    // QStandardPaths::AppDataLocation/firmware). Tests point this at a
    // QTemporaryDir to isolate from the user's real cache.
    void setCacheRoot(const QString& absolutePath);

    // Release channel. Switching channels wipes the on-disk cache so the
    // next checkForUpdate()/downloadIfNeeded() contacts the new source.
    // Default is Channel::Stable.
    Channel channel() const { return m_channel; }
    void setChannel(Channel channel);

    // Current upstream URL (whichever channel is active). Exposed for
    // diagnostics/tests; callers normally don't need to read this.
    const char* currentUrl() const;

    QString cachePath() const;                // <root>/bootfwupdate.dat
    QString metaPath() const;                 // <root>/bootfwupdate.dat.meta.json
    std::optional<Header> cachedHeader() const;
    std::optional<MetaJson> cachedMeta() const { return m_meta; }

    // Wipe the cache file and the sidecar meta. Used when the user resets,
    // or when validation finds a permanently-invalid file.
    void clearCache();

public slots:
    // Cheap availability check. Always emits checkFinished exactly once.
    // `installedVersion` is what's currently on the DE1 (from MMR 0x800010);
    // it drives the Newer/Same classification.
    void checkForUpdate(uint32_t installedVersion);

    // Full download (with Range resume when a partial cache exists).
    // Always emits exactly one of downloadFinished or downloadFailed.
    void downloadIfNeeded();

signals:
    void checkFinished(CheckResult result);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(QString path, DE1::Firmware::Header header);
    void downloadFailed(QString reason);

private slots:
    void onHeadReplyFinished();
    void onHeaderRangeReplyFinished();
    void onDownloadReadyRead();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();

private:
    void ensureCacheDir() const;
    void loadMetaFromDisk();
    void saveMetaToDisk();
    void abortActiveReply();
    void cleanUpDownloadFile();
    void failDownload(const QString& reason);
    void issueHeaderRangeRequest(const QByteArray& newEtag);

    QNetworkAccessManager* m_manager      = nullptr;
    bool                   m_ownsManager  = false;

    QString  m_cacheRoot;                     // set lazily to AppDataLocation
    MetaJson m_meta;
    bool     m_metaLoaded = false;

    QNetworkReply* m_activeReply = nullptr;
    QFile*         m_downloadFile = nullptr;
    QByteArray     m_pendingEtag;             // ETag of in-flight HEAD response
    uint32_t       m_installedVersion = 0;
    Channel        m_channel = Channel::Stable;
};

}  // namespace DE1::Firmware
