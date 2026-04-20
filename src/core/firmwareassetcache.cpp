#include "core/firmwareassetcache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

Q_LOGGING_CATEGORY(firmwareLog, "decenza.firmware")

namespace DE1::Firmware {

namespace {

constexpr qint64 HEADER_RANGE_END = HEADER_SIZE - 1;   // 63, inclusive

QByteArray headerRangeBytes() {
    return QByteArray("bytes=0-") + QByteArray::number(HEADER_RANGE_END);
}

}  // namespace

FirmwareAssetCache::FirmwareAssetCache(QObject* parent)
    : QObject(parent)
{
    // Lazy: real QNetworkAccessManager created on first use, so tests can
    // inject a mock via setNetworkManager() before any network traffic.
}

FirmwareAssetCache::~FirmwareAssetCache() {
    abortActiveReply();
    cleanUpDownloadFile();
    if (m_ownsManager) {
        delete m_manager;
    }
}

void FirmwareAssetCache::setNetworkManager(QNetworkAccessManager* manager) {
    if (m_ownsManager) {
        delete m_manager;
        m_ownsManager = false;
    }
    m_manager = manager;
}

void FirmwareAssetCache::setCacheRoot(const QString& absolutePath) {
    m_cacheRoot = absolutePath;
}

QString FirmwareAssetCache::cachePath() const {
    ensureCacheDir();
    return QDir(m_cacheRoot).filePath(QStringLiteral("bootfwupdate.dat"));
}

QString FirmwareAssetCache::metaPath() const {
    return cachePath() + QStringLiteral(".meta.json");
}

std::optional<Header> FirmwareAssetCache::cachedHeader() const {
    QFile f(cachePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QByteArray bytes = f.read(HEADER_SIZE);
    return parseHeader(bytes);
}

void FirmwareAssetCache::clearCache() {
    abortActiveReply();
    cleanUpDownloadFile();
    QFile::remove(cachePath());
    QFile::remove(metaPath());
    m_meta = {};
    m_metaLoaded = false;
}

void FirmwareAssetCache::ensureCacheDir() const {
    if (m_cacheRoot.isEmpty()) {
        // Const context: m_cacheRoot is mutable-by-design here because the
        // default is computed lazily from user's platform-specific
        // AppDataLocation and shouldn't force eager resolution at ctor time.
        const_cast<FirmwareAssetCache*>(this)->m_cacheRoot =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("firmware"));
    }
    QDir().mkpath(m_cacheRoot);
}

void FirmwareAssetCache::loadMetaFromDisk() {
    if (m_metaLoaded) return;
    m_metaLoaded = true;

    QFile f(metaPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    QByteArray bytes = f.readAll();
    auto parsed = parseMeta(bytes);
    if (parsed) {
        m_meta = *parsed;
    } else {
        qCWarning(firmwareLog) << "Malformed sidecar meta at" << metaPath()
                               << "— ignoring";
    }
}

void FirmwareAssetCache::saveMetaToDisk() {
    ensureCacheDir();
    QSaveFile f(metaPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(firmwareLog) << "Cannot write sidecar meta:" << f.errorString();
        return;
    }
    f.write(serializeMeta(m_meta));
    f.commit();
}

void FirmwareAssetCache::abortActiveReply() {
    if (m_activeReply) {
        m_activeReply->disconnect(this);
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
}

void FirmwareAssetCache::cleanUpDownloadFile() {
    if (m_downloadFile) {
        if (m_downloadFile->isOpen()) {
            m_downloadFile->close();
        }
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
}

void FirmwareAssetCache::failDownload(const QString& reason) {
    abortActiveReply();
    cleanUpDownloadFile();
    emit downloadFailed(reason);
}

// ---------- Availability check ----------

void FirmwareAssetCache::checkForUpdate(uint32_t installedVersion) {
    loadMetaFromDisk();
    m_installedVersion = installedVersion;

    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }

    QNetworkRequest req{QUrl(QString::fromLatin1(FIRMWARE_URL))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_meta.etag.isEmpty()) {
        req.setRawHeader("If-None-Match", m_meta.etag.toUtf8());
    }

    abortActiveReply();
    m_activeReply = m_manager->head(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &FirmwareAssetCache::onHeadReplyFinished);
}

void FirmwareAssetCache::onHeadReplyFinished() {
    QNetworkReply* reply = m_activeReply;
    m_activeReply = nullptr;
    if (!reply) return;

    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();

    if (err == QNetworkReply::ContentNotFoundError || status == 404) {
        reply->deleteLater();
        emit checkFinished({CheckResult::Error, 0,
            QStringLiteral("Firmware URL not found (404). Will disable "
                           "automatic checks until app restart.")});
        return;
    }

    if (err != QNetworkReply::NoError && status != 304) {
        const QString msg = reply->errorString();
        reply->deleteLater();
        emit checkFinished({CheckResult::Error, 0, msg});
        return;
    }

    if (status == 304) {
        // Server confirms our cached ETag is still current — no change.
        reply->deleteLater();
        CheckResult r;
        r.kind          = (m_meta.version > m_installedVersion)
                          ? CheckResult::Newer : CheckResult::Same;
        r.remoteVersion = m_meta.version;
        emit checkFinished(r);
        return;
    }

    // 200 OK on a HEAD means the ETag is new (or was unknown). Pull the
    // 64-byte header via Range to read the new remote Version cheaply.
    const QByteArray newEtag = reply->rawHeader("ETag");
    reply->deleteLater();
    issueHeaderRangeRequest(newEtag);
}

void FirmwareAssetCache::issueHeaderRangeRequest(const QByteArray& newEtag) {
    QNetworkRequest req{QUrl(QString::fromLatin1(FIRMWARE_URL))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Range", headerRangeBytes());

    m_pendingEtag = newEtag;
    m_activeReply = m_manager->get(req);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &FirmwareAssetCache::onHeaderRangeReplyFinished);
}

void FirmwareAssetCache::onHeaderRangeReplyFinished() {
    QNetworkReply* reply = m_activeReply;
    m_activeReply = nullptr;
    if (!reply) return;

    const QNetworkReply::NetworkError err = reply->error();
    if (err != QNetworkReply::NoError) {
        const QString msg = reply->errorString();
        reply->deleteLater();
        emit checkFinished({CheckResult::Error, 0, msg});
        return;
    }

    const QByteArray bytes = reply->readAll();
    reply->deleteLater();

    auto parsed = parseHeader(bytes);
    if (!parsed || parsed->boardMarker != BOARD_MARKER) {
        emit checkFinished({CheckResult::Error, 0,
            QStringLiteral("Remote firmware has invalid header "
                           "(BoardMarker mismatch).")});
        return;
    }

    // Stash the remote version in the sidecar against the new ETag so the
    // next cheap HEAD returns 304 and we skip re-fetching the header.
    m_meta.etag              = QString::fromUtf8(m_pendingEtag);
    m_meta.version           = parsed->version;
    m_meta.downloadedAtEpoch = QDateTime::currentSecsSinceEpoch();
    saveMetaToDisk();

    CheckResult r;
    r.remoteVersion = parsed->version;
    r.kind = (parsed->version > m_installedVersion)
             ? CheckResult::Newer : CheckResult::Same;
    emit checkFinished(r);
}

// ---------- Full download ----------

void FirmwareAssetCache::downloadIfNeeded() {
    loadMetaFromDisk();
    ensureCacheDir();

    // If we already have a validated file matching the sidecar version,
    // skip the download entirely — the caller can use cachedPath/cachedHeader.
    QFileInfo info(cachePath());
    if (info.exists()) {
        auto header = cachedHeader();
        if (header && header->boardMarker == BOARD_MARKER &&
            info.size() >= qint64(header->byteCount) + HEADER_SIZE) {
            emit downloadFinished(cachePath(), *header);
            return;
        }
    }

    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }

    QNetworkRequest req{QUrl(QString::fromLatin1(FIRMWARE_URL))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    // Resume-from-partial if we have usable bytes on disk. The existing
    // partial-size vs. expected-total check is conservative: when we have
    // no previous ETag we don't know the expected total yet, which the
    // rangeHeaderFor() "unknownTotal" branch handles.
    qint64 existing = info.exists() ? info.size() : 0;
    auto rangeHeader = rangeHeaderFor(existing, /*unknown total*/ -1);
    if (rangeHeader) {
        req.setRawHeader("Range", *rangeHeader);
    } else {
        // Either nothing on disk or a weird overrun → wipe and start clean.
        if (existing > 0) {
            QFile::remove(cachePath());
        }
        existing = 0;
    }

    // Open the download file in append-or-create mode. QIODevice::Append is
    // what we want: if Range was set, we pick up where the file left off;
    // otherwise the file was just removed, so there's nothing to append
    // over and Append is equivalent to WriteOnly.
    m_downloadFile = new QFile(cachePath());
    if (!m_downloadFile->open(QIODevice::Append)) {
        const QString msg = QStringLiteral("Cannot open cache file for writing: %1")
                            .arg(m_downloadFile->errorString());
        cleanUpDownloadFile();
        emit downloadFailed(msg);
        return;
    }

    abortActiveReply();
    m_activeReply = m_manager->get(req);
    connect(m_activeReply, &QNetworkReply::readyRead,
            this, &FirmwareAssetCache::onDownloadReadyRead);
    connect(m_activeReply, &QNetworkReply::downloadProgress,
            this, &FirmwareAssetCache::onDownloadProgress);
    connect(m_activeReply, &QNetworkReply::finished,
            this, &FirmwareAssetCache::onDownloadFinished);
}

void FirmwareAssetCache::onDownloadReadyRead() {
    if (!m_activeReply || !m_downloadFile) return;
    QByteArray chunk = m_activeReply->readAll();
    if (!chunk.isEmpty()) {
        m_downloadFile->write(chunk);
    }
}

void FirmwareAssetCache::onDownloadProgress(qint64 received, qint64 total) {
    emit downloadProgress(received, total);
}

void FirmwareAssetCache::onDownloadFinished() {
    QNetworkReply* reply = m_activeReply;
    m_activeReply = nullptr;
    if (!reply) return;

    // Drain any final bytes that arrived between the last readyRead and
    // finished. Qt usually fires both signals for the tail, but being
    // explicit avoids ever writing a short file.
    if (m_downloadFile) {
        m_downloadFile->write(reply->readAll());
        m_downloadFile->flush();
        m_downloadFile->close();
    }

    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        failDownload(errString);
        return;
    }

    auto result = validateFile(cachePath());
    if (result.status != Validation::Ok) {
        // On any validation failure, the cached file is either invalid or
        // incomplete — either way, wipe it so the next retry starts clean
        // rather than trying to resume onto a bad tail.
        QFile::remove(cachePath());
        cleanUpDownloadFile();
        failDownload(result.errorDetail);
        return;
    }

    // Keep the sidecar in sync with what we actually have on disk now.
    m_meta.version           = result.header.version;
    m_meta.downloadedAtEpoch = QDateTime::currentSecsSinceEpoch();
    saveMetaToDisk();
    cleanUpDownloadFile();

    emit downloadFinished(cachePath(), result.header);
}

}  // namespace DE1::Firmware
