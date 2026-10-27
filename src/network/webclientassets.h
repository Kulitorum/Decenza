#pragma once

#include <QByteArray>
#include <QString>

// A bounded, allow-listed reader for the packaged Wasm web client. The caller
// owns HTTP routing, method/auth checks, threading, and socket response.
struct WebClientAssetResult
{
    int statusCode = 503;
    QString contentType = QStringLiteral("text/plain; charset=utf-8");
    QByteArray body;
    QByteArray extraHeaders;
};

// One bundled image at the Wasm client's versioned URL, read from this app's own resources:
// /wasm/emoji/<set hash>/<key>.svg from :/emoji, /wasm/maps/<set hash>/<name> from :/maps.
// Bounded and already in memory, so callers may answer it inline.
bool isWebClientImagePath(const QString& requestPath);
WebClientAssetResult readWebClientImage(const QString& requestPath);

WebClientAssetResult readWebClientAsset(const QString& bundleRoot,
                                        const QString& requestPath,
                                        const QString& expectedPackageId,
                                        const QByteArray& acceptEncoding = {},
                                        const QString& sessionHostId = {});
