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

// The icons a browser asks for, as the resource that answers each path: the
// favicon, and the home-screen icons the web interface declares (iOS 180 px,
// Android 192 px). iOS also asks for the precomposed name on its own. Empty for
// any other path.
inline QString iconResource(const QString& routePath)
{
    if (routePath == QLatin1String("/favicon.ico") || routePath == QLatin1String("/favicon.png"))
        return QStringLiteral(":/favicon.png");
    if (routePath == QLatin1String("/apple-touch-icon.png")
        || routePath == QLatin1String("/apple-touch-icon-precomposed.png"))
        return QStringLiteral(":/apple-touch-icon.png");
    if (routePath == QLatin1String("/android-icon-192.png"))
        return QStringLiteral(":/android-icon-192.png");
    return QString();
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
