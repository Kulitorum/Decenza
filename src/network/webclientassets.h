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

WebClientAssetResult readWebClientAsset(const QString& bundleRoot,
                                        const QString& requestPath,
                                        const QString& expectedPackageId,
                                        const QByteArray& acceptEncoding = {},
                                        const QString& sessionHostId = {});
