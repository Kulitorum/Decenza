#include "ziparchive.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <limits>
#include <quazip/quazipfile.h>

namespace {
bool copy(QIODevice& input, QIODevice& output, qint64 expected = -1)
{
    QByteArray buffer(ZipArchive::ChunkBytes, Qt::Uninitialized);
    qint64 copied = 0;
    for (;;) {
        const qint64 bytes = input.read(buffer.data(), buffer.size());
        if (bytes < 0) return false;
        if (bytes == 0) return input.atEnd();
        if (expected >= 0 && bytes > expected - copied) return false;
        if (output.write(buffer.constData(), bytes) != bytes) return false;
        copied += bytes;
    }
}
}

bool ZipArchive::addFile(QuaZip& archive, const QString& name, QIODevice& input,
                          int level, int method)
{
    archive.setUtf8Enabled(true);
    archive.setZip64Enabled(input.size() >= 0xffffffffLL);
    if (method < 0) {
        static const QStringList compressed{"jpg", "jpeg", "png", "gif", "webp",
                                            "mp4", "mov", "webm", "avi", "zip", "gz"};
        method = compressed.contains(QFileInfo(name).suffix().toLower()) ? 0 : Z_DEFLATED;
    }
    QuaZipFile entry(&archive);
    if (!entry.open(QIODevice::WriteOnly, QuaZipNewInfo(name), nullptr, 0, method, level))
        return false;
    const bool copied = copy(input, entry);
    entry.close();
    return copied && entry.getZipError() == UNZ_OK;
}

bool ZipArchive::addFile(QuaZip& archive, const QString& name, const QByteArray& bytes,
                          int level, int method)
{
    QBuffer input;
    input.setData(bytes);
    return input.open(QIODevice::ReadOnly) && addFile(archive, name, input, level, method);
}

bool ZipArchive::extract(const QString& archivePath, const QString& destination, int* files)
{
    // QuaZip 1.6's minizip treats an EOCD at offset zero as not found
    // (quazip/unzip.c:700-702). Preserve support for the canonical empty ZIP.
    QFile empty(archivePath);
    if (empty.size() == 22 && empty.open(QIODevice::ReadOnly)
        && empty.readAll() == QByteArray::fromHex("504b0506000000000000000000000000000000000000")) {
        if (files) *files = 0;
        return QDir().mkpath(destination);
    }
    QuaZip archive(archivePath);
    if (!archive.open(QuaZip::mdUnzip)) return false;
    const QDir root(destination);
    if (!QDir().mkpath(root.absolutePath())) return false;
    const QString prefix = root.absolutePath() + QLatin1Char('/');
    int count = 0;
    for (bool more = archive.getEntriesCount() > 0 && archive.goToFirstFile();
         more; more = archive.goToNextFile()) {
        QuaZipFileInfo64 info;
        if (!archive.getCurrentFileInfo(&info)) return false;
        if (info.uncompressedSize > quint64(std::numeric_limits<qint64>::max())) return false;
        const QString path = QDir::cleanPath(root.absoluteFilePath(info.name));
        // Reject links and paths outside the staging directory.
        if (info.name.contains(QLatin1Char('\\')) || QDir::isAbsolutePath(info.name)
            || (!path.startsWith(prefix) && path != root.absolutePath())
            || ((info.externalAttr >> 16) & 0170000) == 0120000)
            return false;
        if (info.name.endsWith(QLatin1Char('/'))) {
            if (!QDir().mkpath(path)) return false;
            continue;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QuaZipFile input(&archive);
        QFile output(path);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return false;
        const bool copied = copy(input, output, qint64(info.uncompressedSize));
        const bool sizeMatches = quint64(output.pos()) == info.uncompressedSize;
        input.close();
        if (!copied || !sizeMatches || input.getZipError() != UNZ_OK || !output.flush()) return false;
        ++count;
    }
    const bool success = archive.getZipError() == UNZ_OK;
    archive.close();
    if (files) *files = count;
    return success && archive.getZipError() == UNZ_OK;
}
