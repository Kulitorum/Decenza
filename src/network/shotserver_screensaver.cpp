#include "shotserver.h"

#include "screensavermediarange.h"
#include "screensaver/screensavervideomanager.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

struct PendingScreensaverMediaTransfer
{
    QFile file;
    qint64 remaining = 0;
    QTimer* stallTimer = nullptr;
};

namespace {
constexpr qsizetype kMaximumConcurrentScreensaverTransfers = 4;
constexpr int kMediaWriteStallTimeoutMs = 30000;

QByteArray rangeHeader(const QByteArray& request, bool* duplicate)
{
    if (duplicate)
        *duplicate = false;
    QByteArray result;
    const qsizetype end = request.indexOf(QByteArrayLiteral("\r\n\r\n"));
    const QByteArray headers = request.left(end < 0 ? request.size() : end);
    const auto lines = headers.split('\n');
    bool found = false;
    for (QByteArray line : lines) {
        line = line.trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon < 0 || line.left(colon).compare(QByteArrayLiteral("Range"), Qt::CaseInsensitive) != 0)
            continue;
        if (found) {
            if (duplicate)
                *duplicate = true;
            return {};
        }
        found = true;
        result = line.mid(colon + 1).trimmed();
    }
    return result;
}
}

void ShotServer::handleScreensaverMedia(QTcpSocket* socket, const QString& method,
                                        const QString& path, const QByteArray& request)
{
    static const QRegularExpression route(
        QStringLiteral("^/api/screensaver/media/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})$"));
    const auto match = route.match(path);
    if (method != QStringLiteral("GET") && method != QStringLiteral("HEAD")) {
        sendResponse(socket, 405, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Use GET or HEAD\"}"),
                     QByteArrayLiteral("Allow: GET, HEAD\r\nCache-Control: private, no-store\r\n"));
        return;
    }
    if (!match.hasMatch() || !m_screensaverManager) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Media ticket not found\"}"),
                     QByteArrayLiteral("Cache-Control: private, no-store\r\n"));
        return;
    }

    ScreensaverMediaTicket media;
    if (!m_screensaverManager->resolveMediaTicket(match.captured(1), &media)) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Media ticket not found\"}"),
                     QByteArrayLiteral("Cache-Control: private, no-store\r\n"));
        return;
    }
    if (method == QStringLiteral("GET")
        && m_screensaverTransfers.size() >= kMaximumConcurrentScreensaverTransfers) {
        sendResponse(socket, 503, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Media service is busy\"}"),
                     QByteArrayLiteral("Retry-After: 1\r\nCache-Control: private, no-store\r\n"));
        return;
    }

    const QFileInfo info(media.path);
    if (!info.exists() || !info.isFile() || info.size() != media.size) {
        sendResponse(socket, 404, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Media ticket not found\"}"),
                     QByteArrayLiteral("Cache-Control: private, no-store\r\n"));
        return;
    }

    bool duplicateRange = false;
    const QByteArray rangeValue = rangeHeader(request, &duplicateRange);
    ScreensaverMediaRange::Range range;
    if (duplicateRange || !ScreensaverMediaRange::parse(rangeValue, media.size, &range)) {
        sendResponse(socket, 416, QStringLiteral("application/json"),
                     QByteArrayLiteral("{\"error\":\"Requested byte range is not satisfiable\"}"),
                     QByteArrayLiteral("Accept-Ranges: bytes\r\nContent-Range: bytes */")
                         + QByteArray::number(media.size)
                         + QByteArrayLiteral("\r\nCache-Control: private, no-store\r\n"));
        return;
    }

    QSharedPointer<PendingScreensaverMediaTransfer> transfer;
    if (method == QStringLiteral("GET")) {
        transfer = QSharedPointer<PendingScreensaverMediaTransfer>::create();
        transfer->file.setFileName(media.path);
        if (!transfer->file.open(QIODevice::ReadOnly) || !transfer->file.seek(range.first)) {
            sendResponse(socket, 404, QStringLiteral("application/json"),
                         QByteArrayLiteral("{\"error\":\"Media file is unavailable\"}"),
                         QByteArrayLiteral("Cache-Control: private, no-store\r\n"));
            return;
        }
    }

    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    const int status = range.partial ? 206 : 200;
    const qint64 length = range.length();
    QByteArray headers = QByteArrayLiteral("HTTP/1.1 ")
        + QByteArray::number(status)
        + (range.partial ? QByteArrayLiteral(" Partial Content\r\n")
                         : QByteArrayLiteral(" OK\r\n"));
    headers += QByteArrayLiteral("Content-Type: ") + media.contentType.toLatin1() + QByteArrayLiteral("\r\n");
    headers += QByteArrayLiteral("Content-Length: ") + QByteArray::number(length) + QByteArrayLiteral("\r\n");
    headers += QByteArrayLiteral("Accept-Ranges: bytes\r\nCache-Control: private, no-store\r\nConnection: close\r\n");
    if (range.partial) {
        headers += QByteArrayLiteral("Content-Range: bytes ") + QByteArray::number(range.first)
            + QByteArrayLiteral("-") + QByteArray::number(range.last)
            + QByteArrayLiteral("/") + QByteArray::number(media.size) + QByteArrayLiteral("\r\n");
    }
    headers += QByteArrayLiteral("\r\n");

    if (socket->write(headers) != headers.size()) {
        socket->abort();
        return;
    }
    if (method == QStringLiteral("HEAD")) {
        socket->disconnectFromHost();
        return;
    }

    transfer->remaining = length;
    transfer->stallTimer = new QTimer(socket);
    transfer->stallTimer->setSingleShot(true);
    transfer->stallTimer->setTimerType(Qt::PreciseTimer);
    connect(transfer->stallTimer, &QTimer::timeout, this, [this, socket] {
        if (!m_screensaverTransfers.contains(socket))
            return;
        m_screensaverTransfers.remove(socket);
        socket->abort();
    });
    m_screensaverTransfers.insert(socket, transfer);
    connect(socket, &QTcpSocket::bytesWritten, this, [this, socket](qint64) {
        const auto activeTransfer = m_screensaverTransfers.value(socket);
        if (activeTransfer && activeTransfer->stallTimer)
            activeTransfer->stallTimer->start(kMediaWriteStallTimeoutMs);
        pumpScreensaverMedia(socket);
    });
    transfer->stallTimer->start(kMediaWriteStallTimeoutMs);
    pumpScreensaverMedia(socket);
}

void ShotServer::pumpScreensaverMedia(QTcpSocket* socket)
{
    auto transfer = m_screensaverTransfers.value(socket);
    if (!transfer || !socket || socket->state() != QAbstractSocket::ConnectedState) {
        m_screensaverTransfers.remove(socket);
        return;
    }
    while (transfer->remaining > 0) {
        const qint64 amount = ScreensaverMediaRange::nextChunkSize(
            socket->bytesToWrite(), transfer->remaining);
        if (amount <= 0)
            break;
        const QByteArray bytes = transfer->file.read(amount);
        if (bytes.size() != amount || socket->write(bytes) != amount) {
            m_screensaverTransfers.remove(socket);
            socket->abort();
            return;
        }
        transfer->remaining -= amount;
    }
    if (transfer->remaining == 0 && socket->bytesToWrite() == 0) {
        if (transfer->stallTimer)
            transfer->stallTimer->stop();
        m_screensaverTransfers.remove(socket);
        socket->disconnectFromHost();
    }
}
