#pragma once

#include <QRegularExpression>
#include <QString>

#include "servicevalue.h"

namespace FileBundlePolicy {

inline constexpr int MaximumBundleBytes = 128 * 1024 * 1024;
inline constexpr int MaximumChunkBytes = 48 * 1024;
inline constexpr int MaximumLanguageJsonBytes = 8 * 1024 * 1024;
inline constexpr int IdleTimeoutMs = 5 * 60 * 1000;

inline bool validOpaqueId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9-]{36}$"));
    return pattern.match(id).hasMatch();
}

inline bool validOffset(const QVariant& value, int currentOffset, int totalBytes, int* offset)
{
    int parsed = -1;
    if (!ServiceValue::boundedInteger(value, 0, totalBytes, &parsed)
        || parsed != currentOffset)
        return false;
    if (offset) *offset = parsed;
    return true;
}

inline bool validChunk(int offset, qsizetype bytes, int totalBytes)
{
    return offset >= 0 && bytes > 0 && bytes <= MaximumChunkBytes
        && offset <= totalBytes && bytes <= totalBytes - offset;
}

} // namespace FileBundlePolicy
