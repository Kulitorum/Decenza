#include "shotserver.h"
#include "webclientassets.h"
#include "core/packageidentity.h"

#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

void ShotServer::handleWebClientAsset(QTcpSocket* socket, const QString& method,
                                      const QString& path, const QByteArray& request)
{
    const QByteArray noStore = QByteArrayLiteral("Cache-Control: no-store\r\n");
    if (method != QStringLiteral("GET")) {
        sendResponse(socket, 405, QStringLiteral("text/plain"), QByteArrayLiteral("Use GET"),
                     noStore + QByteArrayLiteral("Allow: GET\r\n"));
        return;
    }
    const QUrl requestUrl = QUrl::fromEncoded(path.toUtf8(), QUrl::StrictMode);
    if (!requestUrl.isValid() || !requestUrl.isRelative() || !requestUrl.authority().isEmpty()) {
        sendResponse(socket, 404, QStringLiteral("text/plain"),
                     QByteArrayLiteral("Not found"), noStore);
        return;
    }
    const QString assetPath = requestUrl.path(QUrl::FullyEncoded);
    if (assetPath == QStringLiteral("/wasm")) {
        QByteArray location = QByteArrayLiteral("Location: /wasm/");
        if (requestUrl.hasQuery())
            location += QByteArrayLiteral("?") + requestUrl.query(QUrl::FullyEncoded).toUtf8();
        location += QByteArrayLiteral("\r\n");
        sendResponse(socket, 302, QStringLiteral("text/plain"), {},
                     noStore + location);
        return;
    }
    if (isWebClientImagePath(assetPath)) {
        // Small, from this app's own resources, and requested a dozen at a time per screen:
        // answered here rather than through the four-slot runtime download pool below.
        const WebClientAssetResult image = readWebClientImage(assetPath);
        sendResponse(socket, image.statusCode, image.contentType, image.body, image.extraHeaders);
        return;
    }
    QString bundleRoot = QStringLiteral(":/webclient");
    if (!QDir(bundleRoot).exists()) {
#ifdef Q_OS_MACOS
        bundleRoot = QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../Resources/webclient"));
#else
        sendResponse(socket, 404, QStringLiteral("text/plain"),
                     QByteArrayLiteral("Web client packaging is not available on this platform yet"), noStore);
        return;
#endif
    }
    if (m_activeWebAssetTransfers >= 4 || socket->bytesToWrite() > 0) {
        sendResponse(socket, 503, QStringLiteral("text/plain"),
                     QByteArrayLiteral("Web client download is busy; retry shortly"), noStore);
        return;
    }
    QByteArray acceptEncoding;
    const qsizetype headerEnd = request.indexOf("\r\n\r\n");
    for (const QByteArray& line : request.left(headerEnd).split('\n')) {
        if (line.toLower().startsWith("accept-encoding:")) {
            if (!acceptEncoding.isEmpty())
                acceptEncoding += ',';
            acceptEncoding += line.mid(16).trimmed();
        }
    }
    QPointer<ShotServer> safeThis(this);
    QPointer<QTcpSocket> safeSocket(socket);
    const QString capturedHostId = m_sessionHostId;
    ++m_activeWebAssetTransfers;
    QThread* thread = QThread::create([safeThis, safeSocket, bundleRoot, assetPath, acceptEncoding, capturedHostId] {
        auto result = readWebClientAsset(bundleRoot, assetPath,
                                        QString::fromLatin1(decenzaPackageId()), acceptEncoding, capturedHostId);
        QMetaObject::invokeMethod(safeThis,
            [safeThis, safeSocket, capturedHostId, response = std::move(result)] {
                if (!safeThis)
                    return;
                if (!safeSocket || safeSocket->state() != QAbstractSocket::ConnectedState) {
                    --safeThis->m_activeWebAssetTransfers;
                    return;
                }
                if (safeThis->m_sessionHostId != capturedHostId) {
                    --safeThis->m_activeWebAssetTransfers;
                    safeThis->sendResponse(safeSocket, 409, QStringLiteral("text/plain"),
                        QByteArrayLiteral("The host restarted. Reload the packaged interface."),
                        QByteArrayLiteral("Cache-Control: no-store\r\n"));
                    return;
                }
                // Keep the download slot until the socket queue drains, including slow readers.
                auto released = std::make_shared<bool>(false);
                auto connections = std::make_shared<QList<QMetaObject::Connection>>();
                const auto release = [safeThis, released, connections] {
                    if (*released)
                        return;
                    *released = true;
                    if (safeThis)
                        --safeThis->m_activeWebAssetTransfers;
                    for (const auto& connection : *connections)
                        QObject::disconnect(connection);
                    connections->clear();
                };
                connections->append(QObject::connect(safeSocket, &QTcpSocket::bytesWritten,
                    safeThis, [safeSocket, release](qint64) {
                        if (!safeSocket || safeSocket->bytesToWrite() == 0)
                            release();
                    }));
                connections->append(QObject::connect(safeSocket, &QTcpSocket::disconnected,
                                                      safeThis, release));
                connections->append(QObject::connect(safeSocket, &QObject::destroyed,
                                                      safeThis, release));
                safeThis->sendResponse(safeSocket, response.statusCode, response.contentType,
                                       response.body, response.extraHeaders);
                if (!safeSocket || safeSocket->bytesToWrite() == 0)
                    release();
            }, Qt::QueuedConnection);
    });
    thread->setParent(this);
    m_backgroundReadThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_backgroundReadThreads.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
