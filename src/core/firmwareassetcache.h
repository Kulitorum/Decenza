#pragma once

#include <cstdint>
#include <optional>

#include <QByteArray>
#include <QList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "core/firmwareheader.h"

// Loads and validates bundled DE1 firmware binaries shipped with Decenza.
// The firmware catalog is Decaid's manifest, included in resources alongside
// the binaries. Two product channels are exposed: Stable and Early access.
//
// Test scaffolding can still point setCacheRoot() at a temporary directory
// containing bootfwupdate.dat; that override keeps FirmwareUpdater state
// machine tests focused on BLE behavior rather than real bundled bytes.

namespace DE1::Firmware {

// Legacy sidecar persistence record retained only for older helper tests and
// for reading any stale files left in AppData by prior Decenza versions. The
// production firmware path no longer writes or depends on sidecar metadata.
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

struct FirmwareCatalogEntry {
    QString id;
    QString source;
    QString machineFamily;
    QStringList supportedModels;
    uint32_t build = 0;
    QString versionLabel;
    QString imageFormat;
    qint64 byteLength = 0;
    QByteArray sha256Hex;
    QString channel;
    QString releaseNotes;
    QString assetPath;
    uint32_t expectedHeaderBoardMarker = 0;
    uint32_t expectedBodyByteCount = 0;
    uint32_t expectedCpuByteCount = 0;
    QString provenance;

    QString resourcePath() const {
        return QStringLiteral(":/") + assetPath;
    }
};

QList<FirmwareCatalogEntry> parseFirmwareManifest(const QByteArray& json,
                                                  QString* error = nullptr);
QString firmwareSha256Hex(const QString& path, QString* error = nullptr);
ValidationResult validateBundledFirmwareFile(const QString& path,
                                             const FirmwareCatalogEntry& entry);

}  // namespace DE1::Firmware

// -----------------------------------------------------------------------
// FirmwareAssetCache — bundled firmware selector + validator.
//
// Keeps the existing check/download signal contract used by FirmwareUpdater,
// but the default implementation is local: checkForUpdate() compares the
// selected manifest entry with the installed DE1 version, and
// downloadIfNeeded() validates the bundled resource before handing its path to
// the flash state machine.
// -----------------------------------------------------------------------

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace DE1::Firmware {

class FirmwareAssetCache : public QObject {
    Q_OBJECT

public:
    enum class Channel {
        Stable      = 0,
        EarlyAccess = 1,
        Nightly     = 1,  // compatibility alias for existing call sites/tests
    };
    Q_ENUM(Channel)

    static constexpr const char* FIRMWARE_MANIFEST_RESOURCE =
        ":/assets/firmware/manifest.json";

    struct CheckResult {
        // Newer: remote > installed (upgrade offered).
        // Older: remote < installed (downgrade offered — matches de1app).
        // Same:  remote == installed (nothing to do).
        // Error: network/parse failure; errorDetail populated.
        enum Kind { Newer, Same, Older, Error };
        Kind     kind          = Error;
        uint32_t remoteVersion = 0;
        QString  errorDetail;                // empty on success
        QString  versionLabel;
        QString  channelLabel;
        QString  releaseNotes;
    };

    explicit FirmwareAssetCache(QObject* parent = nullptr);
    ~FirmwareAssetCache() override;

    // No-op compatibility hook retained for older tests/call sites. The
    // production firmware path no longer performs network requests.
    void setNetworkManager(QNetworkAccessManager* manager);

    // Override the cache root (defaults to
    // QStandardPaths::AppDataLocation/firmware). Tests point this at a
    // QTemporaryDir to isolate from the user's real cache.
    void setCacheRoot(const QString& absolutePath);

    // Product firmware channel. Default is Channel::Stable.
    Channel channel() const { return m_channel; }
    void setChannel(Channel channel);

    // Compatibility diagnostic string: now the selected bundled resource path.
    QString currentUrl() const;

    QString cachePath() const;                // <root>/bootfwupdate.dat
    QString metaPath() const;                 // <root>/bootfwupdate.dat.meta.json
    std::optional<Header> cachedHeader() const;
    std::optional<MetaJson> cachedMeta() const { return m_meta; }

    // True when `headerVersion` agrees with the version the sidecar recorded
    // for the ETag this channel is currently serving — the check that tells a
    // genuine partial of this revision apart from a leftover of another one.
    bool versionMatchesMeta(uint32_t headerVersion) const;

    std::optional<FirmwareCatalogEntry> selectedEntry(QString* error = nullptr) const;
    QString selectedVersionLabel() const;
    QString selectedChannelLabel() const;
    QString selectedReleaseNotes() const;
    bool usesBundledSource() const { return !hasCacheOverride(); }

    // Wipe legacy cache override files. Bundled resources are never removed.
    void clearCache();

public slots:
    // Cheap availability check. Always emits checkFinished exactly once.
    // `installedVersion` is what's currently on the DE1 (from MMR 0x800010);
    // it drives the Newer/Same classification.
    void checkForUpdate(uint32_t installedVersion);

    // Validate the selected bundled file, or the test override cache file.
    // Always emits exactly one of downloadFinished or downloadFailed.
    void downloadIfNeeded();

signals:
    void checkFinished(CheckResult result);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(QString path, DE1::Firmware::Header header);
    void downloadFailed(QString reason);

private:
    bool hasCacheOverride() const;
    void ensureCacheDir() const;
    void loadMetaFromDisk();
    void saveMetaToDisk();
    void failDownload(const QString& reason);

    QNetworkAccessManager* m_manager      = nullptr;
    bool                   m_ownsManager  = false;

    QString  m_cacheRoot;                     // set lazily to AppDataLocation
    MetaJson m_meta;
    bool     m_metaLoaded = false;

    uint32_t       m_installedVersion = 0;
    Channel        m_channel = Channel::Stable;
};

}  // namespace DE1::Firmware
