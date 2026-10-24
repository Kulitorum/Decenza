#pragma once

#include <QString>

namespace ShotServerWebRoute {

inline bool isLegacyPath(const QString& path)
{
    return path == QLatin1String("/legacy")
        || path.startsWith(QLatin1String("/legacy/"))
        || path.startsWith(QLatin1String("/legacy?"));
}

inline QString legacyTarget(const QString& path)
{
    if (!isLegacyPath(path))
        return path;
    QString target = path.mid(7);
    if (target.isEmpty() || target.startsWith(QLatin1Char('?')))
        target.prepend(QLatin1Char('/'));
    return target;
}

inline QString wasmRootTarget(const QString& path)
{
    if (path == QLatin1String("/"))
        return QStringLiteral("/wasm/");
    if (path.startsWith(QLatin1String("/?")))
        return QStringLiteral("/wasm/") + path.mid(1);
    return path;
}

} // namespace ShotServerWebRoute
