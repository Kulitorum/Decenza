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

const char* FirmwareAssetCache::currentUrl() const {
    return m_channel == Channel::Nightly ? FIRMWARE_URL_NIGHTLY
                                         : FIRMWARE_URL_STABLE;
}

void FirmwareAssetCache::setChannel(Channel channel) {
    if (m_channel == channel) return;
    m_channel = channel;
    // Switching channels invalidates any cached blob: the two endpoints
    // serve different firmware revisions, and their ETags are unrelated.
    // Wipe everything so the next check/download starts from scratch.
    clearCache();
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

bool FirmwareAssetCache::versionMatchesMeta(uint32_t headerVersion) const {
    // The sidecar's version is what the server reported for the ETag it is
    // currently serving on this channel, read from a 64-byte Range GET during
    // the availability check. A cached file whose header disagrees is not the
    // file the check was about: a leftover from the other channel, or a body
    // spliced from an older revision. Zero means we have never completed a
    // check, so there is nothing to contradict and the file is taken as-is.
    return m_meta.version == 0 || m_meta.version == headerVersion;
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

    QNetworkRequest req{QUrl(QString::fromLatin1(currentUrl()))};
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
                          ? CheckResult::Newer
                          : (m_meta.version < m_installedVersion)
                            ? CheckResult::Older
                            : CheckResult::Same;
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
    QNetworkRequest req{QUrl(QString::fromLatin1(currentUrl()))};
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
             ? CheckResult::Newer
             : (parsed->version < m_installedVersion)
               ? CheckResult::Older
               : CheckResult::Same;
    emit checkFinished(r);
}

// ---------- Full download ----------

void FirmwareAssetCache::downloadIfNeeded() {
    loadMetaFromDisk();
    ensureCacheDir();

    // If we already have a validated file matching the sidecar version,
    // skip the download entirely — the caller can use cachedPath/cachedHeader.
    // Byte offset a resume should continue from; 0 means "download the whole
    // body". Tracked explicitly rather than re-derived from the file's size
    // later, so that a failed delete cannot silently turn a rejected file
    // back into a resume target.
    qint64 resumeFrom = 0;

    QFileInfo info(cachePath());
    qCDebug(firmwareLog) << "[firmware] downloadIfNeeded: channel="
                         << currentUrl() << "existingBytes="
                         << (info.exists() ? info.size() : 0)
                         << "metaVersion=" << m_meta.version
                         << "metaEtag=" << m_meta.etag;
    if (info.exists()) {
        auto validated = validateFile(cachePath());
        if (validated.status == Validation::Ok &&
            versionMatchesMeta(validated.header.version)) {
            // Short-circuit: the on-disk file is accepted without contacting
            // the server. Log the version we hand back so a mismatch against
            // the UI's "Available:" is visible in the trail.
            qCDebug(firmwareLog) << "[firmware] using cached file without "
                                    "download: version=" << validated.header.version
                                 << "size=" << info.size();
            emit downloadFinished(cachePath(), validated.header);
            return;
        }
        // Not usable as-is. Resuming onto it is only safe when the bytes on
        // disk are a genuine prefix of the revision we are about to fetch —
        // i.e. an interrupted download of *this* file. That is exactly the
        // Truncated case with a matching Version.
        //
        // Any other state must be wiped rather than resumed. The resume below
        // sends `Range: bytes=<size>-` and appends, so resuming onto a
        // different revision splices that revision's body to the new one's
        // tail. The result is a file of the correct total length whose first
        // 64 bytes are a valid header — it passes BoardMarker, the size
        // floor and every structural check, and the corruption is only
        // discovered by the DE1 at the end of a ~18-minute flash.
        //
        // A known sidecar version is required, not merely a matching one:
        // m_meta.version == 0 means we have never completed a check (or the
        // sidecar is unreadable), so there is nothing to compare against and
        // "matches" would be vacuously true — exactly the state the guard
        // exists to reject. A full re-download is cheap next to flashing a
        // wrong image.
        const bool resumable =
            validated.status == Validation::Truncated &&
            m_meta.version != 0 &&
            versionMatchesMeta(validated.header.version);
        if (resumable) {
            resumeFrom = info.size();
            qCDebug(firmwareLog) << "[firmware] resuming interrupted download of "
                                    "version" << validated.header.version
                                 << "from byte" << resumeFrom;
        } else {
            qCWarning(firmwareLog) << "[firmware] discarding cached file:"
                                   << (validated.status == Validation::Ok
                                           ? QStringLiteral("header version %1 is not the %2 "
                                                            "the server reported for this ETag")
                                                 .arg(validated.header.version).arg(m_meta.version)
                                           : validated.errorDetail);
            // Deliberately not gated on the result: a failed remove (locked
            // file, permissions) must not leave us resuming onto the bytes we
            // just rejected. resumeFrom stays 0 and the file is opened with
            // Truncate below, so the restart is clean whether or not this
            // succeeded.
            (void)QFile::remove(cachePath());
        }
    }

    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
        m_ownsManager = true;
    }

    QNetworkRequest req{QUrl(QString::fromLatin1(currentUrl()))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    auto rangeHeader = rangeHeaderFor(resumeFrom, /*unknown total*/ -1);
    m_resumedDownload = rangeHeader.has_value();
    if (rangeHeader) {
        req.setRawHeader("Range", *rangeHeader);
    }

    // Append only when resuming; otherwise Truncate. Truncate is what makes
    // the restart safe without trusting the delete above to have worked — an
    // Append onto a file that failed to delete would splice the whole body
    // onto the rejected bytes, which is the corruption this all exists to
    // prevent.
    m_downloadFile = new QFile(cachePath());
    const auto openMode = m_resumedDownload
                          ? QIODevice::Append
                          : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!m_downloadFile->open(openMode)) {
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

    // A resume is only a resume if the server honoured the Range. Servers,
    // proxies and redirect targets are all free to ignore it and answer 200
    // with the WHOLE body — and NoLessSafeRedirectPolicy is set, so a
    // redirect that drops the header is reachable. Appending a full body to a
    // partial produces an oversized file whose header is the partial's, which
    // passes the version check (that is why the resume was allowed) and every
    // structural check. Abandon the resume and restart clean instead.
    if (m_resumedDownload && !m_rangeHonoured) {
        const int status = m_activeReply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 206) {
            qCWarning(firmwareLog) << "[firmware] resume requested but server answered"
                                   << status << "- restarting the download from scratch";
            abortActiveReply();
            cleanUpDownloadFile();
            (void)QFile::remove(cachePath());
            m_resumedDownload = false;
            downloadIfNeeded();   // cache file is gone, so this cannot re-resume
            return;
        }
        m_rangeHonoured = true;
    }

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

    const bool wasResumed = m_resumedDownload;
    m_resumedDownload = false;
    m_rangeHonoured   = false;

    auto result = validateFile(cachePath());
    if (result.status == Validation::Ok && wasResumed &&
        !versionMatchesMeta(result.header.version)) {
        // A RESUMED body whose header is not the revision we expected is a
        // splice: old bytes on disk, new bytes appended.
        //
        // This must not be applied to a fresh, complete download. startUpdate()
        // goes straight to downloadIfNeeded() with no new HEAD, so m_meta
        // holds whatever the last availability check wrote — 30 s after launch,
        // then weekly. On a tablet left running for days, against a channel
        // that republishes (nightly does, constantly), a perfectly good
        // single-revision download can legitimately be newer than the sidecar.
        // Rejecting that would accuse the server of splicing, delete the file,
        // and leave Retry looping on the identical rejection until the next
        // scheduled check — unrecoverable from the UI.
        result.status = Validation::MalformedHeader;
        result.errorDetail = QStringLiteral(
            "Resumed download reports version %1, but the partial on disk was "
            "version %2. The file was assembled from more than one revision; "
            "it has been discarded.")
            .arg(result.header.version).arg(m_meta.version);
    }
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
