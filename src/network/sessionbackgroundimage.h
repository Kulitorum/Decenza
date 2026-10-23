#pragma once

#include <QByteArray>
#include <QString>

namespace SessionBackgroundImage {

inline constexpr qint64 MaximumSourceBytes = 32 * 1024 * 1024;
inline constexpr qint64 MaximumOutputBytes = 8 * 1024 * 1024;
inline constexpr int MaximumDimensionPixels = 32 * 1000 * 1000;
inline constexpr int MaximumEdgePixels = 2560;

struct Result {
    QByteArray bytes;
    QString contentType;
};

// Resolve, validate, decode, and if necessary resize/encode one selected local
// PNG/JPEG. Error text is suitable for a response and never contains a path.
bool load(const QString& selectedPath, const QString& allowedRoot,
          Result* result, QString* error = nullptr);

} // namespace SessionBackgroundImage
