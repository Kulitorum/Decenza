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
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <cmath>

#include "decenza_image_sets.h"

namespace {

constexpr qint64 kManifestLimit = 64 * 1024;
constexpr qint64 kTextAssetLimit = 10 * 1024 * 1024;
constexpr qint64 kWasmAssetLimit = 128 * 1024 * 1024;
// The largest bundled Twemoji SVG is 51 KB (1f1e8-1f1f6.svg); the largest earth texture is 407 KB.
constexpr qint64 kEmojiImageLimit = 256 * 1024;
constexpr qint64 kMapImageLimit = 2 * 1024 * 1024;

constexpr char kNoSniffHeader[] = "X-Content-Type-Options: nosniff\r\n";
constexpr char kImmutableHeader[] = "Cache-Control: private, max-age=31536000, immutable\r\n";
constexpr char kNoStoreHeader[] = "Cache-Control: no-store\r\n";

// The images the host serves to the Wasm client from its own resources, at
// /wasm/<set>/<set content hash>/<file>.
struct ImageSet {
    QLatin1String prefix;
    QRegularExpression pattern;
    QLatin1String hash;
    QString resourceDir;
    qint64 maxBytes;
};

ImageSet imageSet(const char* prefix, const char* namePattern, const char* hash,
                  const QString& resourceDir, qint64 maxBytes)
{
    return {QLatin1String(prefix),
            QRegularExpression(QLatin1Char('^') + QRegularExpression::escape(QLatin1String(prefix))
                               + QStringLiteral("([0-9a-f]{64})/(") + QLatin1String(namePattern)
                               + QStringLiteral(")$")),
            QLatin1String(hash), resourceDir, maxBytes};
}

const QList<ImageSet>& imageSets()
{
    static const QList<ImageSet> sets{
        imageSet("/wasm/emoji/", "[0-9a-f]+(?:-[0-9a-f]+)*\\.svg", DECENZA_EMOJI_SET_HASH,
                 QStringLiteral(":/emoji/"), kEmojiImageLimit),
        imageSet("/wasm/maps/", "[a-z_]+\\.(?:png|jpg)", DECENZA_MAP_SET_HASH,
                 QStringLiteral(":/maps/"), kMapImageLimit),
    };
    return sets;
}

QString imageContentType(const QString& name)
{
    if (name.endsWith(QStringLiteral(".svg")))
        return QStringLiteral("image/svg+xml");
    if (name.endsWith(QStringLiteral(".png")))
        return QStringLiteral("image/png");
    return QStringLiteral("image/jpeg");
}

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
    result.extraHeaders = QByteArray(kNoStoreHeader) + kNoSniffHeader;
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
    double brotli = -1.0;
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
        if (name == "br")
            result.brotli = quality;
        else if (name == "*")
            result.wildcard = quality;
        else if (name == "identity")
            result.identity = quality;
    }
    if (duplicate.contains("br"))
        result.brotli = 0.0;
    if (duplicate.contains("*"))
        result.wildcard = 0.0;
    if (duplicate.contains("identity"))
        result.identity = 0.0;
    return result;
}

double brotliQuality(const EncodingQuality& quality)
{
    return quality.brotli >= 0.0 ? quality.brotli : (quality.wildcard >= 0.0 ? quality.wildcard : 0.0);
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

bool isWebClientImagePath(const QString& requestPath)
{
    for (const ImageSet& set : imageSets()) {
        if (requestPath.startsWith(set.prefix))
            return true;
    }
    return false;
}

WebClientAssetResult readWebClientImage(const QString& requestPath)
{
    const QUrl requestUrl = QUrl::fromEncoded(requestPath.toUtf8(), QUrl::StrictMode);
    if (!requestUrl.isValid() || !requestUrl.isRelative() || !requestUrl.authority().isEmpty())
        return failure(404, QStringLiteral("Image not found"));
    const QString path = requestUrl.path(QUrl::FullyEncoded);
    for (const ImageSet& set : imageSets()) {
        const QRegularExpressionMatch match = set.pattern.match(path);
        // Another set hash means the client and host come from different builds; the package
        // identity check reports that, so the image just isn't here.
        if (!match.hasMatch() || match.captured(1) != set.hash)
            continue;
        const QString name = match.captured(2);
        QFile image(set.resourceDir + name);
        if (!image.open(QIODevice::ReadOnly) || image.size() > set.maxBytes)
            break;
        WebClientAssetResult result;
        result.statusCode = 200;
        result.contentType = imageContentType(name);
        result.body = image.read(set.maxBytes);
        result.extraHeaders = QByteArray(kNoSniffHeader) + kImmutableHeader;
        return result;
    }
    return failure(404, QStringLiteral("Image not found"));
}

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
    const QString relativePath = path.mid(static_cast<qsizetype>(sizeof(prefix) - 1));
    const qsizetype separator = relativePath.indexOf(QLatin1Char('/'));
    const bool versioned = separator >= 0;
    if (versioned && !isSha256(relativePath.left(separator)))
        return failure(404, QStringLiteral("Web client asset not found"));
    const QString assetName = relativePath.isEmpty()
        ? QStringLiteral("index.html")
        : relativePath.mid(versioned ? separator + 1 : 0);
    const auto asset = allowedAssets().constFind(assetName);
    if (asset == allowedAssets().cend())
        return failure(404, QStringLiteral("Web client asset not found"));
    const QByteArray responseHeaders = QByteArrayLiteral("Vary: Accept-Encoding\r\n") + kNoSniffHeader
        + (versioned && assetName != QStringLiteral("index.html") ? kImmutableHeader : kNoStoreHeader);

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
    // Source identity pairs the kits; the manifest's asset hashes identify the actual build bytes.
    const QString versionPrefix = QStringLiteral("/wasm/")
        + QString::fromLatin1(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256).toHex())
        + QLatin1Char('/');
    if (versioned && !path.startsWith(versionPrefix))
        return failure(404, QStringLiteral("Web client asset not found"));
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
    const bool brotliOk = !bootstrapEntry && brotliQuality(encoding) > 0.0;
    const bool hasBrotliSize = assetEntry.contains(QStringLiteral("brSize"));
    const bool hasBrotliHash = assetEntry.contains(QStringLiteral("brSha256"));
    if (hasBrotliSize != hasBrotliHash)
        return failure(503, QStringLiteral("Packaged web client Brotli metadata is invalid"));
    if (brotliOk && hasBrotliSize) {
        qint64 brotliSize = 0;
        QString brotliHash;
        if (!requiredSizeAndHash(assetEntry, QStringLiteral("brSize"),
                                 QStringLiteral("brSha256"), asset->maxBytes,
                                 &brotliSize, &brotliHash)) {
            return failure(503, QStringLiteral("Packaged web client Brotli metadata is invalid"));
        }
        QByteArray body;
        if (!readVerified(canonicalRoot, assetName + QStringLiteral(".br"), brotliSize,
                          brotliHash, asset->maxBytes, &body)) {
            return failure(503, QStringLiteral("Packaged web client asset failed integrity validation"));
        }
        WebClientAssetResult result;
        result.statusCode = 200;
        result.contentType = asset->contentType;
        result.body = body;
        result.extraHeaders = QByteArrayLiteral("Content-Encoding: br\r\n") + responseHeaders;
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
            "<base href=\"%3\">"
            "<meta name=\"decenza-session-package\" content=\"%1\">"
            "<meta name=\"decenza-session-host\" content=\"%2\">")
            .arg(expectedPackageId.toHtmlEscaped(), sessionHostId.toHtmlEscaped(), versionPrefix).toUtf8();
        body.insert(head + 6, bootstrap);
    }
    WebClientAssetResult result;
    result.statusCode = 200;
    result.contentType = asset->contentType;
    result.body = body;
    result.extraHeaders = responseHeaders;
    return result;
}
