#include "personalmediapreparation.h"
#include "diagnosticlogging.h"
#include "network/exifdate.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QMimeType>
#include <QStringList>
#include <QPainter>
#include <exception>
#ifndef Q_OS_IOS
#include <QProcess>
#endif

namespace PersonalMediaPreparation {
#ifndef Q_OS_IOS
namespace {
bool finishProcess(QProcess& process, int timeoutMs, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if (!cancelled) {
        if (process.waitForFinished(timeoutMs)) return true;
    } else {
        QDeadlineTimer deadline(timeoutMs);
        while (process.state() != QProcess::NotRunning && !deadline.hasExpired()) {
            if (cancelled->load(std::memory_order_relaxed)) break;
            if (process.waitForFinished(int(qMin<qint64>(100, deadline.remainingTime())))) return true;
        }
        if (process.state() == QProcess::NotRunning
            && !cancelled->load(std::memory_order_relaxed)) return true;
    }
    process.kill();
    process.waitForFinished();
    return false;
}
} // namespace
#endif

namespace {
bool writeDecodedImage(const QImage& image, const QString& inputPath,
                       const QString& outputPath, int maxWidth, int maxHeight)
{
    // Scale maintaining aspect ratio (fit within bounds)
    QImage scaled = image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) {
        DIAG_WARN(NETWORK, "MediaPreparation") << "Failed to scale image (memory?):" << inputPath;
        return false;
    }

    // Create target-sized canvas with black background (letterbox/pillarbox)
    QImage result(maxWidth, maxHeight, QImage::Format_RGB32);
    result.fill(Qt::black);

    // Center the scaled image on the canvas using QPainter
    int x = (maxWidth - scaled.width()) / 2;
    int y = (maxHeight - scaled.height()) / 2;

    QPainter painter(&result);
    painter.drawImage(x, y, scaled);
    painter.end();

    // Save with good quality
    QString ext = QFileInfo(outputPath).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg") {
        return result.save(outputPath, "JPEG", 85);
    } else if (ext == "png") {
        return result.save(outputPath, "PNG");
    } else {
        return result.save(outputPath);
    }
}
} // namespace

bool resizeImage(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight)
{
    try {
        QImage image(inputPath);
        if (image.isNull()) {
            DIAG_WARN(NETWORK, "MediaPreparation") << "Failed to load image:" << inputPath;
            return false;
        }

        return writeDecodedImage(image, inputPath, outputPath, maxWidth, maxHeight);
    } catch (const std::exception& e) {
        DIAG_WARN(NETWORK, "MediaPreparation") << "Exception in resizeImage:" << e.what();
        return false;
    } catch (...) {
        DIAG_WARN(NETWORK, "MediaPreparation") << "Unknown exception in resizeImage";
        return false;
    }
}

bool resizeVideo(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight,
                 const std::shared_ptr<std::atomic_bool>& cancelled)
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS - can't run FFmpeg
    Q_UNUSED(inputPath)
    Q_UNUSED(outputPath)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(cancelled)
    DIAG_WARN(NETWORK, "MediaPreparation") << "Video resizing not supported on iOS (no QProcess)";
    return false;
#else
    // Use FFmpeg for video resizing
    // FFmpeg command: ffmpeg -i input.mp4 -vf "scale='min(1920,iw)':min'(1080,ih)':force_original_aspect_ratio=decrease" -c:a copy output.mp4

    QString ffmpegPath = "ffmpeg";  // Assume it's in PATH

#ifdef Q_OS_WIN
    // On Windows, try common locations if not in PATH
    QStringList possiblePaths = {
        "ffmpeg",
        "C:/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files/ffmpeg/bin/ffmpeg.exe",
        QCoreApplication::applicationDirPath() + "/ffmpeg.exe"
    };

    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "ffmpeg") {
            ffmpegPath = path;
            break;
        }
    }
#endif

    // Build filter chain: scale to fit, then pad with black to exact size (letterbox/pillarbox)
    QString filterChain = QString(
        "scale='min(%1,iw)':'min(%2,ih)':force_original_aspect_ratio=decrease,"
        "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black"
    ).arg(maxWidth).arg(maxHeight);

    QStringList args;
    args << "-y"  // Overwrite output
         << "-i" << inputPath
         << "-vf" << filterChain
         << "-c:v" << "libx264"
         << "-preset" << "fast"
         << "-crf" << "23"
         << "-c:a" << "aac"
         << "-b:a" << "128k"
         << outputPath;

    DIAG_DEBUG(NETWORK, "MediaPreparation") << "Running FFmpeg:" << ffmpegPath << args.join(" ");

    QProcess process;
    process.start(ffmpegPath, args);

    if (!process.waitForStarted(5000)) {
        DIAG_WARN(NETWORK, "MediaPreparation") << "FFmpeg failed to start. Is it installed?";
        return false;
    }

    // Wait up to 5 minutes for video processing
    if (!finishProcess(process, 300000, cancelled)) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return false;
        DIAG_WARN(NETWORK, "MediaPreparation") << "FFmpeg timeout";
        process.kill();
        return false;
    }

    if (process.exitCode() != 0) {
        DIAG_WARN(NETWORK, "MediaPreparation") << "FFmpeg error:" << process.readAllStandardError();
        return false;
    }

    DIAG_DEBUG(NETWORK, "MediaPreparation") << "FFmpeg completed successfully";
    return QFile::exists(outputPath);
#endif
}

QDateTime extractDateWithExiftool(const QString& filePath, const std::shared_ptr<std::atomic_bool>& cancelled)
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS
    Q_UNUSED(filePath)
    Q_UNUSED(cancelled)
    return QDateTime();
#else
    // Use exiftool for robust date extraction from any format
    QString exiftoolPath = "exiftool";

#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "exiftool",
        "C:/exiftool/exiftool.exe",
        "C:/Program Files/exiftool/exiftool.exe",
        QCoreApplication::applicationDirPath() + "/exiftool.exe"
    };
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "exiftool") {
            exiftoolPath = path;
            break;
        }
    }
#endif

    QStringList args;
    args << "-DateTimeOriginal" << "-CreateDate" << "-s3" << "-d" << "%Y-%m-%d %H:%M:%S" << filePath;

    QProcess process;
    process.start(exiftoolPath, args);

    if (!process.waitForStarted(3000) || !finishProcess(process, 10000, cancelled)) {
        return QDateTime();
    }

    QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        // Take first non-empty line (DateTimeOriginal preferred)
        QString dateStr = output.split('\n').first().trimmed();
        QDateTime dt = QDateTime::fromString(dateStr, "yyyy-MM-dd HH:mm:ss");
        if (dt.isValid()) {
            DIAG_DEBUG(NETWORK, "MediaPreparation") << "Exiftool extracted date:" << dt;
            return dt;
        }
    }

    return QDateTime();
#endif
}

QDateTime extractImageDate(const QString& imagePath, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    // Try exiftool first (handles all formats including RAW/HEIC)
    QDateTime dt = extractDateWithExiftool(imagePath, cancelled);
    if (dt.isValid()) {
        return dt;
    }

    // Fallback: try to extract EXIF DateTimeOriginal from JPEG files manually
    QFile file(imagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QDateTime();
    }

    const QByteArray data = file.read(65536);  // First 64KB is where EXIF lives
    file.close();

    const QDateTime parsedDt = ExifDate::fromJpegBytes(data);
    if (parsedDt.isValid())
        DIAG_DEBUG(NETWORK, "MediaPreparation") << "Extracted EXIF date:" << parsedDt;
    return parsedDt;
}

QDateTime extractVideoDate(const QString& videoPath, const std::shared_ptr<std::atomic_bool>& cancelled)
{
#ifdef Q_OS_IOS
    // QProcess is not available on iOS
    Q_UNUSED(videoPath)
    Q_UNUSED(cancelled)
    return QDateTime();
#else
    // Use FFprobe to extract creation_time metadata
    QString ffprobePath = "ffprobe";

#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "ffprobe",
        "C:/ffmpeg/bin/ffprobe.exe",
        "C:/Program Files/ffmpeg/bin/ffprobe.exe",
        QCoreApplication::applicationDirPath() + "/ffprobe.exe"
    };
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path) || path == "ffprobe") {
            ffprobePath = path;
            break;
        }
    }
#endif

    QStringList args;
    args << "-v" << "quiet"
         << "-print_format" << "json"
         << "-show_format"
         << videoPath;

    QProcess process;
    process.start(ffprobePath, args);

    if (!process.waitForStarted(3000) || !finishProcess(process, 10000, cancelled)) {
        return QDateTime();
    }

    QByteArray output = process.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject()) {
        return QDateTime();
    }

    // Look for creation_time in format tags
    QJsonObject root = doc.object();
    QJsonObject format = root["format"].toObject();
    QJsonObject tags = format["tags"].toObject();

    QString creationTime = tags["creation_time"].toString();
    if (creationTime.isEmpty()) {
        creationTime = tags["com.apple.quicktime.creationdate"].toString();  // iOS
    }

    if (!creationTime.isEmpty()) {
        // Parse ISO 8601 format: "2024-01-15T10:30:00.000000Z"
        QDateTime dt = QDateTime::fromString(creationTime.left(19), "yyyy-MM-ddTHH:mm:ss");
        if (dt.isValid()) {
            DIAG_DEBUG(NETWORK, "MediaPreparation") << "Extracted video date:" << dt;
            return dt;
        }
    }

    return QDateTime();
#endif
}

Result prepare(const QString& inputPath, const QString& originalName,
               const QString& outputDirectory, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    Result result;
    const auto isCancelled = [&cancelled] { return cancelled->load(std::memory_order_relaxed); };
    if (isCancelled()) { result.reason = QStringLiteral("cancelled"); return result; }
    const QFileInfo input(inputPath);
    const QString ext = QFileInfo(originalName).suffix().toLower();
    const bool image = QStringList{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("gif"), QStringLiteral("webp")}.contains(ext);
    const bool video = QStringList{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mov")}.contains(ext);
    if ((!image && !video) || originalName != QFileInfo(originalName).fileName()
        || originalName.contains(QLatin1Char('\\'))
        || input.isSymLink() || !input.isFile() || input.size() <= 0 || input.size() > MaximumFileBytes) {
        result.reason = QStringLiteral("invalidMedia"); return result;
    }
    QImage decoded;
    if (image) {
        QImageReader reader(inputPath);
        reader.setDecideFormatFromContent(true);
        const QByteArray format = reader.format().toLower();
        const QByteArray expected = (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
            ? QByteArrayLiteral("jpeg") : ext.toLatin1();
        const QSize size = reader.size();
        if (!reader.canRead() || (format != expected && !(expected == "jpeg" && format == "jpg"))
            || !size.isValid()) {
            result.reason = QStringLiteral("invalidMedia"); return result;
        }
        // Decode before accepting: a matching signature with truncated pixels
        // is not a usable image. Keep Qt's existing decoder allocation cap
        // (Qt 6.12, qtbase/src/gui/image/qimageiohandler.cpp:542-550).
        decoded = reader.read();
        if (decoded.isNull()) { result.reason = QStringLiteral("invalidMedia"); return result; }
        result.mediaDate = extractImageDate(inputPath, cancelled);
    } else {
        const QMimeType type = QMimeDatabase().mimeTypeForFile(inputPath, QMimeDatabase::MatchContent);
        const QString expected = ext == QLatin1String("mov") ? QStringLiteral("video/quicktime")
            : ext == QLatin1String("webm") ? QStringLiteral("video/webm") : QStringLiteral("video/mp4");
        if (!type.inherits(expected)) { result.reason = QStringLiteral("invalidMedia"); return result; }
        result.mediaDate = extractVideoDate(inputPath, cancelled);
    }
    if (isCancelled()) { result.reason = QStringLiteral("cancelled"); return result; }
    const QString output = QDir(outputDirectory).filePath(originalName);
    bool resized = false;
    if (image) {
        try {
            resized = writeDecodedImage(decoded, inputPath, output, 1280, 800);
        } catch (const std::exception& e) {
            DIAG_WARN(NETWORK, "MediaPreparation") << "Exception in resizeImage:" << e.what();
        } catch (...) {
            DIAG_WARN(NETWORK, "MediaPreparation") << "Unknown exception in resizeImage";
        }
    } else {
        resized = resizeVideo(inputPath, output, 1280, 800, cancelled);
    }
    if (isCancelled()) { result.reason = QStringLiteral("cancelled"); return result; }
    if (!resized) {
        // Keep existing GIF/video behavior when an encoder is unavailable.
        QFile::remove(output);
        if (!QFile::copy(inputPath, output)) {
            result.reason = QStringLiteral("mediaPreparationFailed"); return result;
        }
    }
    if (QFileInfo(output).size() <= 0 || QFileInfo(output).size() > MaximumFileBytes) {
        result.reason = QStringLiteral("mediaPreparationFailed"); return result;
    }
    result.path = output;
    return result;
}
} // namespace PersonalMediaPreparation
