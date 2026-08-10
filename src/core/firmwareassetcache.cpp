#include "core/firmwareassetcache.h"

#include <limits>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(firmwareLog, "decenza.firmware")

namespace DE1::Firmware {

namespace {

QString productLabelForChannel(FirmwareAssetCache::Channel channel) {
    return channel == FirmwareAssetCache::Channel::Stable
        ? QStringLiteral("Stable")
        : QStringLiteral("Early access");
}

QString artifactIdForChannel(FirmwareAssetCache::Channel channel) {
    return channel == FirmwareAssetCache::Channel::Stable
        ? QStringLiteral("de1-1352")
        : QStringLiteral("de1-1358");
}

bool requireString(const QJsonObject& obj, const QString& key, QString* out,
                   QString* error) {
    const QJsonValue v = obj.value(key);
    if (!v.isString()) {
        if (error) *error = QStringLiteral("manifest field %1 is not a string").arg(key);
        return false;
    }
    *out = v.toString();
    return true;
}

bool requireUInt32(const QJsonObject& obj, const QString& key, uint32_t* out,
                   QString* error) {
    const QJsonValue v = obj.value(key);
    if (!v.isDouble()) {
        if (error) *error = QStringLiteral("manifest field %1 is not a number").arg(key);
        return false;
    }
    const qint64 n = v.toVariant().toLongLong();
    if (n < 0 || n > std::numeric_limits<uint32_t>::max()) {
        if (error) *error = QStringLiteral("manifest field %1 is out of range").arg(key);
        return false;
    }
    *out = static_cast<uint32_t>(n);
    return true;
}

bool requireInt64(const QJsonObject& obj, const QString& key, qint64* out,
                  QString* error) {
    const QJsonValue v = obj.value(key);
    if (!v.isDouble()) {
        if (error) *error = QStringLiteral("manifest field %1 is not a number").arg(key);
        return false;
    }
    const qint64 n = v.toVariant().toLongLong();
    if (n < 0) {
        if (error) *error = QStringLiteral("manifest field %1 is negative").arg(key);
        return false;
    }
    *out = n;
    return true;
}

}  // namespace

QList<FirmwareCatalogEntry> parseFirmwareManifest(const QByteArray& json,
                                                  QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("manifest JSON parse failed: %1")
                         .arg(parseError.errorString());
        }
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonValue artifactsValue = root.value(QStringLiteral("artifacts"));
    if (!artifactsValue.isArray()) {
        if (error) *error = QStringLiteral("manifest artifacts field is not an array");
        return {};
    }

    QList<FirmwareCatalogEntry> entries;
    const QJsonArray artifacts = artifactsValue.toArray();
    for (const QJsonValue& value : artifacts) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("manifest artifact is not an object");
            return {};
        }

        const QJsonObject obj = value.toObject();
        FirmwareCatalogEntry entry;
        QString sha256;
        if (!requireString(obj, QStringLiteral("id"), &entry.id, error) ||
            !requireString(obj, QStringLiteral("source"), &entry.source, error) ||
            !requireString(obj, QStringLiteral("machineFamily"), &entry.machineFamily, error) ||
            !requireUInt32(obj, QStringLiteral("build"), &entry.build, error) ||
            !requireString(obj, QStringLiteral("versionLabel"), &entry.versionLabel, error) ||
            !requireString(obj, QStringLiteral("imageFormat"), &entry.imageFormat, error) ||
            !requireInt64(obj, QStringLiteral("byteLength"), &entry.byteLength, error) ||
            !requireString(obj, QStringLiteral("sha256"), &sha256, error) ||
            !requireString(obj, QStringLiteral("channel"), &entry.channel, error) ||
            !requireString(obj, QStringLiteral("releaseNotes"), &entry.releaseNotes, error) ||
            !requireString(obj, QStringLiteral("assetPath"), &entry.assetPath, error) ||
            !requireUInt32(obj, QStringLiteral("expectedHeaderBoardMarker"),
                           &entry.expectedHeaderBoardMarker, error) ||
            !requireUInt32(obj, QStringLiteral("expectedBodyByteCount"),
                           &entry.expectedBodyByteCount, error) ||
            !requireUInt32(obj, QStringLiteral("expectedCpuByteCount"),
                           &entry.expectedCpuByteCount, error) ||
            !requireString(obj, QStringLiteral("provenance"), &entry.provenance, error)) {
            return {};
        }

        const QJsonValue modelsValue = obj.value(QStringLiteral("supportedModels"));
        if (!modelsValue.isArray()) {
            if (error) *error = QStringLiteral("manifest supportedModels field is not an array");
            return {};
        }
        for (const QJsonValue& modelValue : modelsValue.toArray()) {
            if (!modelValue.isString()) {
                if (error) *error = QStringLiteral("manifest supportedModels entry is not a string");
                return {};
            }
            entry.supportedModels.append(modelValue.toString());
        }

        entry.sha256Hex = sha256.toLatin1().toLower();
        entries.append(entry);
    }

    return entries;
}

QString firmwareSha256Hex(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open firmware file for digest: %1")
                         .arg(file.errorString());
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error) *error = QStringLiteral("Cannot read firmware file for digest");
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

ValidationResult validateBundledFirmwareFile(const QString& path,
                                             const FirmwareCatalogEntry& entry) {
    ValidationResult result = validateFile(path);
    if (result.status != Validation::Ok) {
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.status = Validation::UnreadableFile;
        result.errorDetail = QStringLiteral("Cannot open bundled firmware file: %1")
                                 .arg(file.errorString());
        return result;
    }
    const qint64 fileSize = file.size();
    file.close();
    if (fileSize != entry.byteLength) {
        result.status = Validation::MalformedHeader;
        result.errorDetail = QStringLiteral(
            "Bundled firmware size mismatch: have %1 bytes, manifest expected %2")
            .arg(fileSize).arg(entry.byteLength);
        return result;
    }

    if (result.header.version != entry.build ||
        result.header.boardMarker != entry.expectedHeaderBoardMarker ||
        result.header.byteCount != entry.expectedBodyByteCount ||
        result.header.cpuBytes != entry.expectedCpuByteCount) {
        result.status = Validation::MalformedHeader;
        result.errorDetail = QStringLiteral(
            "Bundled firmware header does not match Decaid manifest entry %1")
            .arg(entry.id);
        return result;
    }

    QString digestError;
    const QString digest = firmwareSha256Hex(path, &digestError);
    if (digest.isEmpty()) {
        result.status = Validation::UnreadableFile;
        result.errorDetail = digestError;
        return result;
    }
    if (digest.toLatin1() != entry.sha256Hex) {
        result.status = Validation::MalformedHeader;
        result.errorDetail = QStringLiteral(
            "Bundled firmware digest mismatch for %1").arg(entry.id);
        return result;
    }

    return result;
}

FirmwareAssetCache::FirmwareAssetCache(QObject* parent)
    : QObject(parent)
{
}

FirmwareAssetCache::~FirmwareAssetCache() {
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

bool FirmwareAssetCache::hasCacheOverride() const {
    return !m_cacheRoot.isEmpty();
}

QString FirmwareAssetCache::currentUrl() const {
    QString error;
    const auto entry = selectedEntry(&error);
    if (!entry) {
        return QString::fromLatin1(FIRMWARE_MANIFEST_RESOURCE);
    }
    return entry->resourcePath();
}

void FirmwareAssetCache::setChannel(Channel channel) {
    if (m_channel == channel) return;
    m_channel = channel;
    if (hasCacheOverride()) {
        clearCache();
    }
}

QString FirmwareAssetCache::cachePath() const {
    if (!hasCacheOverride()) {
        QString error;
        const auto entry = selectedEntry(&error);
        return entry ? entry->resourcePath() : QString();
    }
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
    return m_meta.version == 0 || m_meta.version == headerVersion;
}

void FirmwareAssetCache::clearCache() {
    if (!hasCacheOverride()) {
        m_meta = {};
        m_metaLoaded = false;
        return;
    }
    QFile::remove(cachePath());
    QFile::remove(metaPath());
    m_meta = {};
    m_metaLoaded = false;
}

void FirmwareAssetCache::ensureCacheDir() const {
    if (m_cacheRoot.isEmpty()) {
        const_cast<FirmwareAssetCache*>(this)->m_cacheRoot =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("firmware"));
    }
    QDir().mkpath(m_cacheRoot);
}

void FirmwareAssetCache::loadMetaFromDisk() {
    if (m_metaLoaded || !hasCacheOverride()) return;
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
                               << "- ignoring";
    }
}

void FirmwareAssetCache::saveMetaToDisk() {
    if (!hasCacheOverride()) return;
    ensureCacheDir();
    QSaveFile f(metaPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(firmwareLog) << "Cannot write sidecar meta:" << f.errorString();
        return;
    }
    f.write(serializeMeta(m_meta));
    f.commit();
}

void FirmwareAssetCache::failDownload(const QString& reason) {
    emit downloadFailed(reason);
}

std::optional<FirmwareCatalogEntry> FirmwareAssetCache::selectedEntry(QString* error) const {
    QFile manifest(QString::fromLatin1(FIRMWARE_MANIFEST_RESOURCE));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open bundled firmware manifest: %1")
                         .arg(manifest.errorString());
        }
        return std::nullopt;
    }

    QString parseError;
    const QList<FirmwareCatalogEntry> entries =
        parseFirmwareManifest(manifest.readAll(), &parseError);
    if (entries.isEmpty()) {
        if (error) *error = parseError;
        return std::nullopt;
    }

    const QString wantedId = artifactIdForChannel(m_channel);
    for (const FirmwareCatalogEntry& entry : entries) {
        if (entry.id == wantedId) {
            return entry;
        }
    }

    if (error) {
        *error = QStringLiteral("Bundled firmware manifest does not contain %1")
                     .arg(wantedId);
    }
    return std::nullopt;
}

QString FirmwareAssetCache::selectedVersionLabel() const {
    QString error;
    const auto entry = selectedEntry(&error);
    return entry ? entry->versionLabel : QString();
}

QString FirmwareAssetCache::selectedChannelLabel() const {
    return productLabelForChannel(m_channel);
}

QString FirmwareAssetCache::selectedReleaseNotes() const {
    QString error;
    const auto entry = selectedEntry(&error);
    return entry ? entry->releaseNotes : QString();
}

void FirmwareAssetCache::checkForUpdate(uint32_t installedVersion) {
    m_installedVersion = installedVersion;

    if (hasCacheOverride()) {
        loadMetaFromDisk();
        const auto header = cachedHeader();
        if (!header) {
            CheckResult errorResult;
            errorResult.kind = CheckResult::Error;
            errorResult.errorDetail = QStringLiteral("Firmware file missing or unreadable");
            emit checkFinished(errorResult);
            return;
        }
        CheckResult r;
        r.remoteVersion = header->version;
        r.kind = (header->version > installedVersion)
             ? CheckResult::Newer
             : (header->version < installedVersion)
               ? CheckResult::Older
               : CheckResult::Same;
        r.versionLabel = QString::number(header->version);
        r.channelLabel = productLabelForChannel(m_channel);
        emit checkFinished(r);
        return;
    }

    QString error;
    const auto entry = selectedEntry(&error);
    if (!entry) {
        CheckResult errorResult;
        errorResult.kind = CheckResult::Error;
        errorResult.errorDetail = error;
        emit checkFinished(errorResult);
        return;
    }

    CheckResult r;
    r.remoteVersion = entry->build;
    r.kind = (entry->build > installedVersion)
         ? CheckResult::Newer
         : (entry->build < installedVersion)
           ? CheckResult::Older
           : CheckResult::Same;
    r.versionLabel = entry->versionLabel;
    r.channelLabel = productLabelForChannel(m_channel);
    r.releaseNotes = entry->releaseNotes;
    emit checkFinished(r);
}

void FirmwareAssetCache::downloadIfNeeded() {
    if (hasCacheOverride()) {
        auto result = validateFile(cachePath());
        if (result.status != Validation::Ok) {
            failDownload(result.errorDetail);
            return;
        }
        emit downloadFinished(cachePath(), result.header);
        return;
    }

    QString error;
    const auto entry = selectedEntry(&error);
    if (!entry) {
        failDownload(error);
        return;
    }

    const QString path = entry->resourcePath();
    auto result = validateBundledFirmwareFile(path, *entry);
    if (result.status != Validation::Ok) {
        failDownload(result.errorDetail);
        return;
    }

    emit downloadProgress(entry->byteLength, entry->byteLength);
    emit downloadFinished(path, result.header);
}

}  // namespace DE1::Firmware
