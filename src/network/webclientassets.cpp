#include "webclientassets.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include <cmath>

namespace {

constexpr qint64 kManifestLimit = 64 * 1024;
constexpr qint64 kTextAssetLimit = 10 * 1024 * 1024;
constexpr qint64 kWasmAssetLimit = 128 * 1024 * 1024;

struct AssetSpec {
    QString contentType;
    qint64 maxBytes = kTextAssetLimit;
};

const QMap<QString, AssetSpec>& allowedAssets()
{
    static const QMap<QString, AssetSpec> assets{
        {QStringLiteral("index.html"), {QStringLiteral("text/html; charset=utf-8"), kTextAssetLimit}},
        {QStringLiteral("decenza-web.js"), {QStringLiteral("text/javascript; charset=utf-8"), kTextAssetLimit}},
        {QStringLiteral("decenza-web.wasm"), {QStringLiteral("application/wasm"), kWasmAssetLimit}},
        {QStringLiteral("qtloader.js"), {QStringLiteral("text/javascript; charset=utf-8"), kTextAssetLimit}},
        {QStringLiteral("qt-dialog-accessibility.js"), {QStringLiteral("text/javascript; charset=utf-8"), kTextAssetLimit}},
    };
    return assets;
}

WebClientAssetResult failure(int status, const QString& message)
{
    WebClientAssetResult result;
    result.statusCode = status;
    result.body = message.toUtf8();
    result.extraHeaders = QByteArrayLiteral("Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n");
    return result;
}

bool isSha256(const QString& value)
{
    if (value.size() != 64)
        return false;
    for (const QChar ch : value) {
        const ushort c = ch.unicode();
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

bool containedRegularFile(const QString& canonicalRoot, const QString& filePath,
                          QString* canonicalFile)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile())
        return false;
    if (canonicalRoot == QStringLiteral(":/webclient")) {
        // Embedded resources have no symlinks. Accept only this fixed root;
        // the caller still enforces the asset allowlist and manifest hashes.
        if (!filePath.startsWith(QStringLiteral(":/webclient/"))
            || QDir::cleanPath(filePath) != filePath)
            return false;
        *canonicalFile = filePath;
        return true;
    }
    const QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty())
        return false;
    const QString prefix = canonicalRoot.endsWith(QLatin1Char('/'))
        ? canonicalRoot : canonicalRoot + QLatin1Char('/');
    if (!resolved.startsWith(prefix, Qt::CaseSensitive))
        return false;
    *canonicalFile = resolved;
    return true;
}

bool readVerified(const QString& canonicalRoot, const QString& relativeName,
                  qint64 declaredSize, const QString& declaredHash,
                  qint64 maxBytes, QByteArray* bytes)
{
    if (declaredSize <= 0 || declaredSize > maxBytes || !isSha256(declaredHash))
        return false;

    QString resolved;
    if (!containedRegularFile(canonicalRoot, canonicalRoot + QLatin1Char('/') + relativeName,
                              &resolved)) {
        return false;
    }

    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly) || file.size() != declaredSize)
        return false;
    const QByteArray data = file.read(declaredSize + 1);
    if (data.size() != declaredSize || file.error() != QFile::NoError)
        return false;
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()
        != declaredHash.toLatin1()) {
        return false;
    }
    *bytes = data;
    return true;
}

struct EncodingQuality {
    double gzip = -1.0;
    double wildcard = -1.0;
    double identity = -1.0;
};

EncodingQuality parseAcceptEncoding(const QByteArray& header)
{
    EncodingQuality result;
    QSet<QByteArray> seen;
    QSet<QByteArray> duplicate;
    for (const QByteArray& rawPart : header.split(',')) {
        const QList<QByteArray> parts = rawPart.trimmed().split(';');
        if (parts.isEmpty())
            continue;
        const QByteArray name = parts.first().trimmed().toLower();
        if (name.isEmpty())
            continue;
        if (seen.contains(name))
            duplicate.insert(name);
        seen.insert(name);
        double quality = 1.0;
        bool qualitySeen = false;
        bool malformed = false;
        for (qsizetype i = 1; i < parts.size(); ++i) {
            const QByteArray parameter = parts.at(i).trimmed();
            const qsizetype equals = parameter.indexOf('=');
            if (equals < 0) {
                malformed = true;
                continue;
            }
            if (parameter.left(equals).trimmed().compare("q", Qt::CaseInsensitive) != 0)
                continue;
            if (qualitySeen) {
                malformed = true;
                continue;
            }
            qualitySeen = true;
            bool ok = false;
            quality = parameter.mid(equals + 1).trimmed().toDouble(&ok);
            if (!ok || quality < 0.0 || quality > 1.0)
                malformed = true;
        }
        if (malformed)
            quality = 0.0;
        if (name == "gzip")
            result.gzip = quality;
        else if (name == "*")
            result.wildcard = quality;
        else if (name == "identity")
            result.identity = quality;
    }
    if (duplicate.contains("gzip"))
        result.gzip = 0.0;
    if (duplicate.contains("*"))
        result.wildcard = 0.0;
    if (duplicate.contains("identity"))
        result.identity = 0.0;
    return result;
}

double gzipQuality(const EncodingQuality& quality)
{
    return quality.gzip >= 0.0 ? quality.gzip : (quality.wildcard >= 0.0 ? quality.wildcard : 0.0);
}

bool identityAllowed(const EncodingQuality& quality)
{
    if (quality.identity >= 0.0)
        return quality.identity > 0.0;
    return !(quality.wildcard == 0.0);
}

bool requiredSizeAndHash(const QJsonObject& object, const QString& sizeKey,
                         const QString& hashKey, qint64 maxBytes,
                         qint64* size, QString* hash)
{
    const QJsonValue sizeValue = object.value(sizeKey);
    const QJsonValue hashValue = object.value(hashKey);
    if (!sizeValue.isDouble() || !hashValue.isString())
        return false;
    const double numericSize = sizeValue.toDouble();
    if (numericSize < 1 || numericSize > static_cast<double>(maxBytes)
        || std::floor(numericSize) != numericSize) {
        return false;
    }
    *size = static_cast<qint64>(numericSize);
    *hash = hashValue.toString();
    return isSha256(*hash);
}

} // namespace

WebClientAssetResult readWebClientAsset(const QString& bundleRoot,
                                        const QString& requestPath,
                                        const QString& expectedPackageId,
                                        const QByteArray& acceptEncoding,
                                        const QString& sessionHostId)
{
    if (!isSha256(expectedPackageId))
        return failure(503, QStringLiteral("Invalid expected web client package identity"));

    const QUrl requestUrl = QUrl::fromEncoded(requestPath.toUtf8(), QUrl::StrictMode);
    if (!requestUrl.isValid() || !requestUrl.isRelative() || !requestUrl.authority().isEmpty())
        return failure(404, QStringLiteral("Web client asset not found"));
    const QString path = requestUrl.path(QUrl::FullyEncoded);
    constexpr char prefix[] = "/wasm/";
    if (!path.startsWith(QLatin1String(prefix)))
        return failure(404, QStringLiteral("Web client asset not found"));
    const QString assetName = path == QStringLiteral("/wasm/")
        ? QStringLiteral("index.html")
        : path.mid(static_cast<qsizetype>(sizeof(prefix) - 1));
    const auto asset = allowedAssets().constFind(assetName);
    if (asset == allowedAssets().cend())
        return failure(404, QStringLiteral("Web client asset not found"));

    const QFileInfo rootInfo(bundleRoot);
    if (!rootInfo.exists() || !rootInfo.isDir())
        return failure(503, QStringLiteral("Packaged web client is unavailable"));
    const QString canonicalRoot = bundleRoot == QStringLiteral(":/webclient")
        ? bundleRoot : rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty())
        return failure(503, QStringLiteral("Packaged web client is unavailable"));

    QString manifestPath;
    if (!containedRegularFile(canonicalRoot, canonicalRoot + QLatin1Char('/')
                                  + QStringLiteral("package.json"), &manifestPath)) {
        return failure(503, QStringLiteral("Packaged web client manifest is unavailable"));
    }
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > kManifestLimit)
        return failure(503, QStringLiteral("Packaged web client manifest is invalid"));
    const QByteArray manifestBytes = manifestFile.read(kManifestLimit + 1);
    if (manifestBytes.size() > kManifestLimit || manifestFile.error() != QFile::NoError)
        return failure(503, QStringLiteral("Packaged web client manifest is invalid"));
    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject())
        return failure(503, QStringLiteral("Packaged web client manifest is invalid"));
    const QJsonObject manifest = manifestDoc.object();
    const QJsonValue packageIdValue = manifest.value(QStringLiteral("packageId"));
    if (!packageIdValue.isString() || !isSha256(packageIdValue.toString()))
        return failure(503, QStringLiteral("Packaged web client identity is invalid"));
    if (packageIdValue.toString() != expectedPackageId)
        return failure(409, QStringLiteral("Packaged web client does not match this application"));
    const QJsonValue filesValue = manifest.value(QStringLiteral("files"));
    if (!filesValue.isObject())
        return failure(503, QStringLiteral("Packaged web client manifest is invalid"));
    const QJsonObject files = filesValue.toObject();
    const QJsonValue assetValue = files.value(assetName);
    if (!assetValue.isObject())
        return failure(503, QStringLiteral("Packaged web client asset is not listed"));
    const QJsonObject assetEntry = assetValue.toObject();
    qint64 identitySize = 0;
    QString identityHash;
    if (!requiredSizeAndHash(assetEntry, QStringLiteral("size"), QStringLiteral("sha256"),
                             asset->maxBytes, &identitySize, &identityHash)) {
        return failure(503, QStringLiteral("Packaged web client asset metadata is invalid"));
    }

    const bool bootstrapEntry = assetName == QStringLiteral("index.html") && !sessionHostId.isEmpty();
    const EncodingQuality encoding = parseAcceptEncoding(acceptEncoding);
    const bool identityOk = identityAllowed(encoding);
    const bool gzipOk = !bootstrapEntry && gzipQuality(encoding) > 0.0;
    const bool hasGzipSize = assetEntry.contains(QStringLiteral("gzipSize"));
    const bool hasGzipHash = assetEntry.contains(QStringLiteral("gzipSha256"));
    if (hasGzipSize != hasGzipHash)
        return failure(503, QStringLiteral("Packaged web client gzip metadata is invalid"));
    if (gzipOk && hasGzipSize) {
        qint64 gzipSize = 0;
        QString gzipHash;
        if (!requiredSizeAndHash(assetEntry, QStringLiteral("gzipSize"),
                                 QStringLiteral("gzipSha256"), asset->maxBytes,
                                 &gzipSize, &gzipHash)) {
            return failure(503, QStringLiteral("Packaged web client gzip metadata is invalid"));
        }
        QByteArray body;
        if (!readVerified(canonicalRoot, assetName + QStringLiteral(".gz"), gzipSize,
                          gzipHash, asset->maxBytes, &body)) {
            return failure(503, QStringLiteral("Packaged web client asset failed integrity validation"));
        }
        WebClientAssetResult result;
        result.statusCode = 200;
        result.contentType = asset->contentType;
        result.body = body;
        result.extraHeaders = QByteArrayLiteral("Content-Encoding: gzip\r\nVary: Accept-Encoding\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n");
        return result;
    }
    if (!identityOk)
        return failure(406, QStringLiteral("No acceptable web client asset encoding"));

    QByteArray body;
    if (!readVerified(canonicalRoot, assetName, identitySize, identityHash,
                      asset->maxBytes, &body)) {
        return failure(503, QStringLiteral("Packaged web client asset failed integrity validation"));
    }
    if (bootstrapEntry) {
        const QUuid host(sessionHostId);
        const qsizetype head = body.indexOf(QByteArrayLiteral("<head>"));
        if (host.isNull() || host.toString(QUuid::WithoutBraces) != sessionHostId || head < 0)
            return failure(503, QStringLiteral("Packaged web client bootstrap identity is unavailable"));
        const QByteArray bootstrap = QStringLiteral(
            "<meta name=\"decenza-session-package\" content=\"%1\">"
            "<meta name=\"decenza-session-host\" content=\"%2\">")
            .arg(expectedPackageId.toHtmlEscaped(), sessionHostId.toHtmlEscaped()).toUtf8();
        body.insert(head + 6, bootstrap);
    }
    WebClientAssetResult result;
    result.statusCode = 200;
    result.contentType = asset->contentType;
    result.body = body;
    result.extraHeaders = QByteArrayLiteral("Vary: Accept-Encoding\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n");
    return result;
}
