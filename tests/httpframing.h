#pragma once

// Reading exactly one HTTP response off a pooled test connection.
//
// Both MCP protocol test binaries hold one socket open across many requests
// (opening a fresh QTcpServer+QTcpSocket per request exhausted the machine's
// ephemeral ports and hung the suite). On a shared connection a bare readAll()
// is wrong: a short read leaves this response's tail to be picked up as the
// head of the next one, and every later assertion in that binary then reads a
// body belonging to an earlier request — surfacing as an unrelated test
// failing. Frame on Content-Length instead.
//
// ONE definition. This was a byte-identical copy in each of the two test files,
// which is the drift the same rule forbids in production code;
// check_test_source_duplication.py cannot see a helper defined inline in a test
// TU, so nothing else would catch the copies diverging.

#include <QByteArray>
#include <QElapsedTimer>
#include <QTcpSocket>

namespace HttpFraming {

// `timeoutMs` bounds the wait for a complete response.
//
// A response with no Content-Length header frames as length 0 — the body ends
// with the headers. McpServer::sendHttpResponse emits the header for every
// status except 204, so a test that starts asserting a 204 has to revisit this;
// none does today.
//
// Returns what arrived, complete or not. A partial buffer is left to the
// caller's own assertions rather than failing here: the caller knows what it
// expected, and a timeout is far more often "the response was never sent" —
// which its next QCOMPARE reports with more context than a bare timeout could.
inline QByteArray readOneResponse(QTcpSocket& client, int timeoutMs = 3000)
{
    QByteArray raw;
    qsizetype headerEnd = -1;
    qint64 contentLength = -1;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (client.bytesAvailable() == 0)
            client.waitForReadyRead(50);
        raw.append(client.readAll());
        if (headerEnd < 0) headerEnd = raw.indexOf("\r\n\r\n");
        if (headerEnd >= 0 && contentLength < 0) {
            contentLength = 0;
            for (const QByteArray& line : raw.left(headerEnd).split('\n')) {
                if (line.trimmed().toLower().startsWith("content-length:"))
                    contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
            }
        }
        if (headerEnd >= 0 && contentLength >= 0
            && raw.size() >= headerEnd + 4 + contentLength)
            break;
    }
    return raw;
}

}  // namespace HttpFraming
