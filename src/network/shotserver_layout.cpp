#include "shotserver.h"
#include "../core/settings.h"
#include "../core/widgetlibrary.h"

#include <QFile>
#include <QUrl>

void ShotServer::handleLayoutApi(QTcpSocket* socket, const QString& method, const QString& path)
{
    if (!m_settings) {
        sendResponse(socket, 500, "application/json", R"({"error":"Settings not available"})");
        return;
    }

    // GET /api/library/thumbnail?id=X[&compact=1] — serve thumbnail PNG
    if (method == "GET" && path.startsWith("/api/library/thumbnail")) {
        if (!m_widgetLibrary) {
            sendResponse(socket, 404, "text/plain", "Not found");
            return;
        }
        qsizetype qIdx = path.indexOf("?id=");
        QString rawId = (qIdx >= 0) ? path.mid(qIdx + 4) : QString();
        qsizetype ampIdx = rawId.indexOf('&');
        if (ampIdx >= 0) rawId = rawId.left(ampIdx);
        QString entryId = QUrl::fromPercentEncoding(rawId.toUtf8());
        bool compact = path.contains("&compact=1");
        QString thumbPath;
        bool exists = false;
        if (!entryId.isEmpty()) {
            if (compact && m_widgetLibrary->hasThumbnailCompact(entryId)) {
                thumbPath = m_widgetLibrary->thumbnailCompactPath(entryId);
                exists = true;
            } else if (m_widgetLibrary->hasThumbnail(entryId)) {
                thumbPath = m_widgetLibrary->thumbnailPath(entryId);
                exists = true;
            }
        }
        if (exists) {
            QFile file(thumbPath);
            if (file.open(QIODevice::ReadOnly)) {
                sendResponse(socket, 200, "image/png", file.readAll());
            } else {
                sendResponse(socket, 404, "text/plain", "No thumbnail");
            }
        } else {
            sendResponse(socket, 404, "text/plain", "No thumbnail");
        }
        return;
    }

    sendResponse(socket, 404, "application/json", R"({"error":"Unknown layout endpoint"})");
}
