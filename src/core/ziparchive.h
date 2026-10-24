#pragma once

#include <QByteArray>
#include <QString>
#include <quazip/quazip.h>

class QIODevice;

namespace ZipArchive {
inline constexpr qint64 ChunkBytes = 64 * 1024;
bool addFile(QuaZip& archive, const QString& name, QIODevice& input,
             int level = 6, int method = -1);
bool addFile(QuaZip& archive, const QString& name, const QByteArray& bytes,
             int level = 6, int method = -1);
bool extract(const QString& archivePath, const QString& destination, int* files = nullptr);
}
