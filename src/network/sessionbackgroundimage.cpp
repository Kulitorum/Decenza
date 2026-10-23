#include "sessionbackgroundimage.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QByteArray>
#include <utility>

namespace SessionBackgroundImage {
namespace {

QString canonicalAllowedPath(const QString& path, const QString& root)
{
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalPath.isEmpty() || canonicalRoot.isEmpty())
        return {};
    const QString relative = QDir(canonicalRoot).relativeFilePath(canonicalPath);
    if (relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative)) {
        return canonicalPath;
    }
    return {};
}

bool encode(const QImage& image, const QByteArray& format, int quality,
            QByteArray* bytes)
{
    bytes->clear();
    QBuffer buffer(bytes);
    if (!buffer.open(QIODevice::WriteOnly))
        return false;
    QImageWriter writer(&buffer, format);
    if (quality >= 0)
        writer.setQuality(quality);
    return writer.write(image);
}

} // namespace

bool load(const QString& selectedPath, const QString& allowedRoot,
          Result* result, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (!result)
        return fail(QStringLiteral("Image response is unavailable"));
    *result = {};

    const QString canonicalPath = canonicalAllowedPath(selectedPath, allowedRoot);
    const QFileInfo info(canonicalPath);
    if (canonicalPath.isEmpty() || !info.exists() || !info.isFile())
        return fail(QStringLiteral("Selected background image is unavailable"));
    const qint64 sourceBytes = info.size();
    if (sourceBytes <= 0 || sourceBytes > MaximumSourceBytes)
        return fail(QStringLiteral("Selected background image exceeds the size limit"));

    const QString suffix = info.suffix().toLower();
    const bool png = suffix == QStringLiteral("png");
    const bool jpeg = suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg");
    if (!png && !jpeg)
        return fail(QStringLiteral("Selected background must be PNG or JPEG"));

    QFile input(canonicalPath);
    if (!input.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Selected background image cannot be read"));
    const QByteArray original = input.read(MaximumSourceBytes + 1);
    if (original.size() != sourceBytes || original.size() > MaximumSourceBytes)
        return fail(QStringLiteral("Selected background image cannot be read"));

    QBuffer sourceBuffer;
    sourceBuffer.setData(original);
    if (!sourceBuffer.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Selected background image cannot be decoded"));
    QImageReader reader(&sourceBuffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    const bool actualPng = format == QByteArrayLiteral("png");
    const bool actualJpeg = format == QByteArrayLiteral("jpg")
        || format == QByteArrayLiteral("jpeg");
    if ((!png && !jpeg) || (!actualPng && !actualJpeg) || (png != actualPng))
        return fail(QStringLiteral("Selected background must be PNG or JPEG"));
    const QSize dimensions = reader.size();
    if (!dimensions.isValid()
        || qint64(dimensions.width()) * dimensions.height() > MaximumDimensionPixels)
        return fail(QStringLiteral("Selected background image dimensions exceed the limit"));
    QImage image = reader.read();
    if (image.isNull() || qint64(image.width()) * image.height() > MaximumDimensionPixels)
        return fail(QStringLiteral("Selected background image cannot be decoded"));

    const bool needsResize = image.width() > MaximumEdgePixels
        || image.height() > MaximumEdgePixels;
    if (needsResize) {
        image = image.scaled(QSize(MaximumEdgePixels, MaximumEdgePixels),
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (!needsResize && sourceBytes <= MaximumOutputBytes) {
        result->bytes = original;
        result->contentType = png ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
    } else {
        QByteArray encoded;
        if (png && image.hasAlphaChannel()) {
            if (!encode(image, QByteArrayLiteral("png"), -1, &encoded)
                || encoded.isEmpty() || encoded.size() > MaximumOutputBytes) {
                return fail(QStringLiteral("Selected background image cannot be reduced to the response limit"));
            }
            result->bytes = std::move(encoded);
            result->contentType = QStringLiteral("image/png");
            if (error)
                error->clear();
            return true;
        }
        if (png && encode(image, QByteArrayLiteral("png"), -1, &encoded)
            && !encoded.isEmpty() && encoded.size() <= MaximumOutputBytes) {
            result->bytes = std::move(encoded);
            result->contentType = QStringLiteral("image/png");
            if (error)
                error->clear();
            return true;
        }
        if (!encode(image, QByteArrayLiteral("jpeg"), 90, &encoded)
            || encoded.isEmpty() || encoded.size() > MaximumOutputBytes) {
            return fail(QStringLiteral("Selected background image cannot be reduced to the response limit"));
        }
        result->bytes = std::move(encoded);
        result->contentType = QStringLiteral("image/jpeg");
    }
    if (error)
        error->clear();
    return true;
}

} // namespace SessionBackgroundImage
