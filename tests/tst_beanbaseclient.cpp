#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QFile>
#include <QScopeGuard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "network/beanbaseclient.h"
#include "network/beanbase_blob.h"
#include "core/settings.h"
#include "mcp/mcptoolregistry.h"


// Registered standalone (mcptools_ai.cpp) precisely so this suite can drive
// the bean_search gather bridge without a MainController.
void registerBeanSearchTool(McpToolRegistry* registry, BeanBaseClient* client);

// A body the image pipeline will accept as a picture: PNG magic plus whatever
// the test wants to recognise on the way out. downloadBagImage refuses bytes
// that are neither labelled nor shaped like an image, which is what stops a
// soft-404 HTML page being cached as a bag photo — so a test that wants the
// bytes cached has to supply something that looks like one.
static QByteArray pngBody(const QByteArray& tag) {
    return QByteArrayLiteral("\x89PNG\r\n\x1a\n") + tag;
}

// Minimal canned-response HTTP server for driving BeanBaseClient. Serves the
// configured status + body to every request, records request lines so tests
// can assert how many requests were actually sent (the whole point of the
// debounce / rate-limit / cache contract) and what query they carried.
class FakeBeanBaseServer : public QObject {
    Q_OBJECT
public:
    FakeBeanBaseServer() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* sock = m_server.nextPendingConnection();
                connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
                    const QByteArray req = sock->readAll();
                    const qsizetype lineEnd = req.indexOf("\r\n");
                    const QString line = QString::fromUtf8(
                        lineEnd > 0 ? req.left(lineEnd) : req);
                    m_requestLines.append(line);
                    if (m_hang)
                        return;  // hold the socket open, never respond: the
                                 // client's reply stays in-flight until aborted.
                    // Redirect routing wins: a matching path 301s to its
                    // Location so validateBagLink's alias-normalization path
                    // (final URL differs from the requested one) is exercisable.
                    bool redirected = false;
                    for (const auto& [pathPart, location] : m_redirects) {
                        if (line.contains(pathPart)) {
                            const QByteArray resp =
                                "HTTP/1.1 301 Moved Permanently\r\n"
                                "Location: " + location.toUtf8() + "\r\n"
                                "Content-Length: 0\r\n"
                                "Connection: close\r\n"
                                "\r\n";
                            sock->write(resp);
                            redirected = true;
                            break;
                        }
                    }
                    if (redirected) {
                        sock->disconnectFromHost();
                        return;
                    }
                    // Per-path routing, first match wins; falls back to the
                    // single canned response. The status follows the SAME
                    // matched route rather than being matched again — an
                    // archive query embeds the product URL it asks about, so a
                    // second independent match would give the archive answer
                    // the product page's status.
                    QByteArray body = m_responseBody;
                    QString matched;
                    for (const auto& [pathPart, pathBody] : m_pathBodies) {
                        if (line.contains(pathPart)) { body = pathBody; matched = pathPart; break; }
                    }
                    QByteArray statusLine = m_statusLine;
                    for (const auto& [pathPart, pathStatus] : m_pathStatuses) {
                        if (pathPart == matched) { statusLine = pathStatus; break; }
                    }
                    const QByteArray resp =
                        "HTTP/1.1 " + statusLine + "\r\n"
                        "Content-Type: " + m_contentType + "\r\n"
                        "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                        "Connection: close\r\n"
                        "\r\n" + body;
                    sock->write(resp);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        const bool ok = m_server.listen(QHostAddress::LocalHost, 0);
        Q_ASSERT(ok);
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    void respondWith(const QByteArray& statusLine, const QByteArray& body) {
        m_statusLine = statusLine;
        m_responseBody = body;
    }

    // Route requests whose request line contains pathPart to a distinct body
    // (first match wins; checked in insertion order).
    // Status for one path, overriding the global respondWith() status. Needed
    // whenever a test serves two different outcomes at once — a dead roaster
    // page beside a healthy archive answer, say.
    void respondForPathWithStatus(const QString& pathPart, const QByteArray& statusLine,
                                  const QByteArray& body) {
        m_pathStatuses.append({pathPart, statusLine});
        m_pathBodies.append({pathPart, body});
    }

    void respondForPath(const QString& pathPart, const QByteArray& body) {
        m_pathBodies.append({pathPart, body});
    }

    // 301 requests whose line contains pathPart to `location` (checked before
    // body routing). Lets a test model a renamed Shopify handle that redirects
    // to the live canonical URL.
    void redirectPath(const QString& pathPart, const QString& location) {
        m_redirects.append({pathPart, location});
    }

    // Accept the request but never answer it — used to keep a reply in-flight so
    // the in-flight supersede/abort path is exercisable.
    void hangWithoutResponding() { m_hang = true; }

    // fetchPageText guards on Content-Type; page-serving tests override the
    // JSON default the API-shaped tests rely on.
    void setContentType(const QByteArray& contentType) { m_contentType = contentType; }

    qsizetype requestCount() const { return m_requestLines.size(); }
    QStringList requestLines() const { return m_requestLines; }

private:
    QTcpServer m_server;
    QByteArray m_statusLine = "200 OK";
    QByteArray m_contentType = "application/json";
    // NOTE: no raw string literals anywhere in this file — moc miscounts the
    // braces inside "..." and silently drops every class declared after one,
    // which breaks the test class's vtable at link time.
    QByteArray m_responseBody = "{\"data\":[]}";
    QList<QPair<QString, QByteArray>> m_pathBodies;
    QList<QPair<QString, QByteArray>> m_pathStatuses;
    QList<QPair<QString, QString>> m_redirects;
    QStringList m_requestLines;
    bool m_hang = false;
};

// A stub standing in for BOTH the roaster and web.archive.org, with the
// snapshot host pointed at it so archiveRawForm recognises what it serves.
struct ArchiveStub {
    FakeBeanBaseServer server;
    ArchiveStub() {
        BeanBaseClient::setArchiveSnapshotHost(
            QStringLiteral("127.0.0.1:%1").arg(server.baseUrl().section(':', -1)));
        server.setContentType("text/html");
    }
    ~ArchiveStub() { BeanBaseClient::setArchiveSnapshotHost(QStringLiteral("web.archive.org")); }
    QString availabilityBody(const QString& snapshotPath) const {
        return QStringLiteral("{\"archived_snapshots\":{\"closest\":{\"status\":\"200\","
                              "\"available\":true,\"url\":\"%1%2\"}}}")
            .arg(server.baseUrl(), snapshotPath);
    }
};

class tst_BeanBaseClient : public QObject {
    Q_OBJECT

private:
    Settings m_settings;
    QNetworkAccessManager m_nam;

private slots:
    void init() { QTest::failOnWarning(); }
    // ====================================================
    // search(): the canonical (Visualizer) path — keyless,
    // debounced (350 ms), session-cached, official /api JSON.
    // ====================================================

    void canonicalSearchFlow() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK",
            "{\"data\":[{\"id\":\"abc-123\",\"canonical_roaster_id\":\"r1\","
            "\"canonical_roaster_name\":\"Prodigal Coffee\",\"name\":\"Milk Blend\","
            "\"country\":\"Brazil\"}]}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchResults);
        // search() is the canonical path: debounced (350 ms), keyless.
        client.search("prodigal mi");
        client.search("prodigal milk");
        QVERIFY(spy.wait(3000));
        QCOMPARE(server.requestCount(), 1);  // debounce coalesced
        QVERIFY(server.requestLines().first().contains("/api/canonical_coffee_bags"));
        const QVariantList entries = spy.first().at(1).toList();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().toMap()["visualizerCanonicalId"].toString(), QString("abc-123"));

        // Cache hit: same query emits synchronously, no second request.
        client.search("Prodigal Milk");
        QCOMPARE(spy.count(), 2);
        QCOMPARE(server.requestCount(), 1);
    }

    void parseCanonicalCoffeeBagsJson() {
        // /api/canonical_coffee_bags response: {data:[…]} with the descriptive
        // block inline; columns remap onto our blob keys, nulls/empties dropped.
        const QByteArray json =
            "{\"data\":[{\"id\":\"e54d274c-fb79\",\"canonical_roaster_id\":\"cb10e43b\","
            "\"canonical_roaster_name\":\"Prodigal Coffee\",\"name\":\"Milk Blend\","
            "\"url\":\"https://x\",\"roast_level\":\"Light To Medium-light\","
            "\"country\":\"Brazil, Colombia\",\"region\":null,\"farmer\":null,"
            "\"processing\":\"Natural\",\"variety\":\"\",\"tasting_notes\":\"Cocoa\"}]}";
        bool ok = false;
        const QVariantList entries = BeanBaseClient::parseCanonicalCoffeeBags(json, &ok);
        QVERIFY(ok);
        QCOMPARE(entries.size(), 1);
        const QVariantMap bag = entries.first().toMap();
        QCOMPARE(bag["id"].toString(), QString("e54d274c-fb79"));
        QCOMPARE(bag["visualizerCanonicalId"].toString(), QString("e54d274c-fb79"));
        QCOMPARE(bag["source"].toString(), QString("visualizer"));
        QCOMPARE(bag["roasterName"].toString(), QString("Prodigal Coffee"));
        QCOMPARE(bag["roastName"].toString(), QString("Milk Blend"));
        QCOMPARE(bag["canonicalRoasterId"].toString(), QString("cb10e43b"));
        QCOMPARE(bag["degree"].toString(), QString("Light To Medium-light"));  // roast_level
        QCOMPARE(bag["origin"].toString(), QString("Brazil, Colombia"));       // country
        QCOMPARE(bag["process"].toString(), QString("Natural"));               // processing
        QCOMPARE(bag["tastingNotes"].toString(), QString("Cocoa"));            // tasting_notes
        QVERIFY(!bag.contains("region"));    // null dropped
        QVERIFY(!bag.contains("producer"));  // farmer null dropped
        QVERIFY(!bag.contains("variety"));   // empty string dropped

        // Malformed JSON -> empty + parsedOk false; valid-but-empty -> empty + true.
        bool okBad = true;
        QCOMPARE(BeanBaseClient::parseCanonicalCoffeeBags("not json", &okBad).size(), 0);
        QVERIFY(!okBad);
        bool okEmpty = false;
        QCOMPARE(BeanBaseClient::parseCanonicalCoffeeBags("{\"data\":[]}", &okEmpty).size(), 0);
        QVERIFY(okEmpty);

        // The product page URL maps onto the blob's `link` key (consumers: the
        // details popup's open-page button, ensureBagImage's og:image source).
        QCOMPARE(bag["link"].toString(), QString("https://x"));
    }

    // ====================================================
    // validateBagLink(): pick-time product-URL validation
    // ====================================================

    void validateBagLinkDeadOn404() {
        // A dead link is only cleared once the archive has CONFIRMED it has no
        // capture: the availability answer below is the well-formed "nothing
        // here" shape.
        FakeBeanBaseServer server;
        server.respondForPath("/wayback/available", "{\"archived_snapshots\":{}}");
        server.respondForPathWithStatus("/products/gone", "404 Not Found", "gone");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);
        QSignalSpy resolvedSpy(&client, &BeanBaseClient::bagLinkResolved);

        client.validateBagLink("canon-1", server.baseUrl() + "/products/gone");
        QVERIFY(deadSpy.wait(3000));
        QCOMPARE(deadSpy.count(), 1);
        QCOMPARE(deadSpy.first().at(0).toString(), QString("canon-1"));
        QCOMPARE(resolvedSpy.count(), 0);  // dead, never "resolved"

        // One GET per (id, url) per session: a second call is a no-op.
        client.validateBagLink("canon-1", server.baseUrl() + "/products/gone");
        QTest::qWait(200);
        QCOMPARE(deadSpy.count(), 1);
    }

    void validateBagLinkResolvedOn200() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK", "<html></html>");
        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy resolvedSpy(&client, &BeanBaseClient::bagLinkResolved);
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);

        const QString productUrl = server.baseUrl() + "/products/live";
        client.validateBagLink("canon-2", productUrl);
        QVERIFY(resolvedSpy.wait(3000));
        QCOMPARE(resolvedSpy.count(), 1);
        QCOMPARE(resolvedSpy.first().at(0).toString(), QString("canon-2"));
        // No redirect here → the final URL is the one we asked for.
        QVERIFY(resolvedSpy.first().at(1).toString().endsWith("/products/live"));
        QCOMPARE(deadSpy.count(), 0);
    }

    // The headline case: a stale Shopify handle 301s to the live product URL.
    // validateBagLink must report the redirect-resolved final URL (not the
    // stale input) so the consumer normalizes the durable canonical link.
    void validateBagLinkNormalizesRedirect() {
        FakeBeanBaseServer server;
        server.redirectPath("/products/old-handle",
                            server.baseUrl() + "/products/new-handle");
        server.respondWith("200 OK", "<html></html>");  // the redirect target
        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy resolvedSpy(&client, &BeanBaseClient::bagLinkResolved);
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);

        const QString staleUrl = server.baseUrl() + "/products/old-handle";
        client.validateBagLink("canon-3", staleUrl);
        QVERIFY(resolvedSpy.wait(3000));
        QCOMPARE(resolvedSpy.count(), 1);
        QCOMPARE(resolvedSpy.first().at(0).toString(), QString("canon-3"));
        const QString resolved = resolvedSpy.first().at(1).toString();
        QVERIFY(resolved.endsWith("/products/new-handle"));  // the live URL
        QVERIFY(resolved != staleUrl);                        // alias normalized
        QCOMPARE(deadSpy.count(), 0);
    }

    // A transient failure (here 503; timeout/DNS/5xx all land here) must emit
    // NEITHER signal, so the link is left intact and a later session retries —
    // clearing it over a blip would wrongly drop a live link.
    void validateBagLinkSilentOnTransientError() {
        FakeBeanBaseServer server;
        server.respondWith("503 Service Unavailable", "busy");
        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy resolvedSpy(&client, &BeanBaseClient::bagLinkResolved);
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);

        client.validateBagLink("canon-4", server.baseUrl() + "/products/flaky");
        QTest::qWait(800);  // give the reply time to finish; nothing should fire
        QCOMPARE(resolvedSpy.count(), 0);
        QCOMPARE(deadSpy.count(), 0);
    }

    // ====================================================
    // Internet Archive fallback for dead product links
    // ====================================================

    // Pure string rewrites: what the app asks the archive for, given a
    // snapshot URL. The `id_` form is the ORIGINAL page bytes (no toolbar, no
    // rewritten asset URLs); the `im_` form is the archive's own copy of one
    // asset, used only when the roaster's copy is gone.
    void archiveUrlForms() {
        const QString snap = "https://web.archive.org/web/20260106073238/"
                             "https://roaster.example/products/x";
        QVERIFY(BeanBaseClient::isArchiveUrl(snap));
        QVERIFY(!BeanBaseClient::isArchiveUrl("https://roaster.example/products/x"));
        QVERIFY(!BeanBaseClient::isArchiveUrl(QString()));

        QCOMPARE(BeanBaseClient::archiveRawForm(snap),
                 QString("https://web.archive.org/web/20260106073238id_/"
                         "https://roaster.example/products/x"));
        // A URL that already carries a modifier is left alone — re-applying
        // would produce `…im_id_/…`, which the archive does not serve.
        const QString already = "https://web.archive.org/web/20260106073238im_/"
                                "https://roaster.example/a.png";
        QCOMPARE(BeanBaseClient::archiveRawForm(already), already);
        // Not a snapshot: unchanged, so every ordinary product URL passes
        // through the same call site untouched.
        QCOMPARE(BeanBaseClient::archiveRawForm("https://roaster.example/p"),
                 QString("https://roaster.example/p"));

        QCOMPARE(BeanBaseClient::archiveAssetForm(snap, "https://cdn.example/a.png"),
                 QString("https://web.archive.org/web/20260106073238im_/"
                         "https://cdn.example/a.png"));
        // No snapshot to hang the asset off, or no asset: nothing to ask for.
        QCOMPARE(BeanBaseClient::archiveAssetForm("https://roaster.example/p",
                                                  "https://cdn.example/a.png"), QString());
        QCOMPARE(BeanBaseClient::archiveAssetForm(snap, QString()), QString());
    }

    void parseArchiveSnapshotVariants() {
        bool ok = false;
        // A usable capture. The API answers in http even for an https capture;
        // the app never stores an http URL it could store as https.
        QCOMPARE(BeanBaseClient::parseArchiveSnapshot(
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"200\",\"available\":true,"
            "\"url\":\"http://web.archive.org/web/20260106073238/https://r.example/p\"}}}", &ok),
            QString("https://web.archive.org/web/20260106073238/https://r.example/p"));
        QVERIFY(ok);

        // Well-formed misses. `ok` stays true: the archive ANSWERED, and the
        // answer was "nothing" — which is what lets the caller clear the link.
        QVERIFY(BeanBaseClient::parseArchiveSnapshot("{\"archived_snapshots\":{}}", &ok).isEmpty());
        QVERIFY(ok);
        QVERIFY(BeanBaseClient::parseArchiveSnapshot(
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"200\",\"available\":false,"
            "\"url\":\"http://web.archive.org/web/1/https://r.example/p\"}}}", &ok).isEmpty());
        QVERIFY(ok);
        // A capture of the roaster's own 404 page is not a capture of the page.
        QVERIFY(BeanBaseClient::parseArchiveSnapshot(
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"404\",\"available\":true,"
            "\"url\":\"http://web.archive.org/web/1/https://r.example/p\"}}}", &ok).isEmpty());
        QVERIFY(ok);
        // Available and 200, but the URL does not parse as a snapshot: the
        // archive has just SAID a capture exists, so this is OUR parser being
        // out of date, not a miss. Reporting it as answered would stamp dead a
        // bag whose capture demonstrably exists.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Availability API reported a capture at an unparseable URL"));
        QVERIFY(BeanBaseClient::parseArchiveSnapshot(
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"200\",\"available\":true,"
            "\"url\":\"https://r.example/p\"}}}", &ok).isEmpty());
        QVERIFY(!ok);

        // Unparseable: `ok` false, so the caller treats it as an archive FAULT
        // and leaves the bag alone rather than stamping it permanently dead.
        QVERIFY(BeanBaseClient::parseArchiveSnapshot("not json", &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(BeanBaseClient::parseArchiveSnapshot("[]", &ok).isEmpty());
        QVERIFY(!ok);

        // JSON, but NOT this API's answer. Well-formedness alone used to count
        // as "the archive said no", so an error body, a proxy interstitial, or
        // a renamed envelope permanently cleared a bag's only URL.
        QVERIFY(BeanBaseClient::parseArchiveSnapshot(
            "{\"error\":\"rate limited\"}", &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(BeanBaseClient::parseArchiveSnapshot("{}", &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(BeanBaseClient::parseArchiveSnapshot(
            "{\"archived_snapshots\":\"soon\"}", &ok).isEmpty());
        QVERIFY(!ok);
    }

    // A content type that is not an image must not be cached as one: the file
    // would exist, so resolution would never run again — across restarts.
    // The soft-404 shapes a CDN actually returns: an S3/GCS XML error body and
    // a RIFF container that is not WebP. Both used to pass the sniff.
    void imageSniffRejectsXmlErrorsAndNonWebpRiff() {
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/product",
            "<html><meta property=\"og:image\" content=\"" + base + "/missing.png\"></html>");
        server.respondForPath("/missing.png",
            "<?xml version=\"1.0\"?><Error><Code>NoSuchKey</Code></Error>");
        server.setContentType("application/xml");

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setImageCacheDir(cacheDir.path());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing unusable image"));
        client.ensureBagImage("canon-xml", "X", server.baseUrl() + "/product");
        QTest::qWait(1200);
        QVERIFY(!QFile::exists(cacheDir.path() + "/canon-xml"));
    }

    void bagImageRefusesANonImageBody() {
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/product",
            "<html><meta property=\"og:image\" content=\"" + base + "/soft404.png\"></html>");
        server.respondForPath("/soft404.png", "<html>not found</html>");
        server.setContentType("text/html");

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setImageCacheDir(cacheDir.path());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Refusing unusable image"));
        client.ensureBagImage("canon-soft404", "X", server.baseUrl() + "/product");
        QTest::qWait(1200);
        QVERIFY(!QFile::exists(cacheDir.path() + "/canon-soft404"));
    }

    // Every probe ANSWERS, including the one that reaches no verdict — the bag
    // editor's suggestion waits on this signal before it can offer a page the
    // user already paid to find, so a silent drop strands it forever.
    void everyProbeAnswersEvenWithoutAVerdict() {
        FakeBeanBaseServer server;
        server.respondForPathWithStatus("/flaky", "503 Service Unavailable", "down");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy spy(&client, &BeanBaseClient::linkStateResolved);

        client.probeLinkState(server.baseUrl() + "/flaky");
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(1).toString(), QString("unknown"));

        // Asked again after the negative cache: no second request, but still an
        // answer, or a caller waiting on the signal waits for a request that
        // was never going to be sent.
        const qsizetype after = server.requestCount();
        client.probeLinkState(server.baseUrl() + "/flaky");
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(1).toString(), QString("unknown"));
        QCOMPARE(server.requestCount(), after);
    }

    // A queued probe dropped when the result set changes answers too: the queue
    // is shared with the suggestion probe, and the drop used to be silent.
    void cancelledQueuedProbesStillAnswer() {
        FakeBeanBaseServer server;
        server.hangWithoutResponding();
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy spy(&client, &BeanBaseClient::linkStateResolved);

        // Fill the in-flight slots, then queue one more.
        for (int i = 0; i < 5; ++i)
            client.probeLinkState(server.baseUrl() + QStringLiteral("/p%1").arg(i));
        QTest::qWait(200);
        client.cancelQueuedLinkProbes();

        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.first().at(1).toString(), QString("unknown"));
    }

    // An unresolvable probe is asked ONCE. Without the negative cache an
    // offline device re-probes every row on every rebuild, all session.
    void linkStateUnresolvableIsNotReProbed() {
        FakeBeanBaseServer server;
        server.respondForPathWithStatus("/flaky", "503 Service Unavailable", "down");

        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        client.probeLinkState(server.baseUrl() + "/flaky");
        QTest::qWait(700);
        QCOMPARE(client.linkState(server.baseUrl() + "/flaky"), QString("unknown"));
        const qsizetype after = server.requestCount();

        client.probeLinkState(server.baseUrl() + "/flaky");
        QTest::qWait(300);
        QCOMPARE(server.requestCount(), after);
    }

    // An archive URL is terminal: validating it would churn the stored link
    // (the archive redirects to a neighbouring capture) and, on a 404, leave
    // the bag re-probing forever.
    void validateBagLinkIgnoresAnArchiveUrl() {
        FakeBeanBaseServer server;
        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy resolvedSpy(&client, &BeanBaseClient::bagLinkResolved);

        client.validateBagLink("canon-arch-5",
                               "https://web.archive.org/web/20260106073238/https://r.example/p");
        QTest::qWait(300);
        QCOMPARE(server.requestCount(), qsizetype(0));
        QCOMPARE(resolvedSpy.count(), 0);
    }

    void validateBagLinkArchivesOn404WithCapture() {
        FakeBeanBaseServer server;
        server.respondForPath("/wayback/available",
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"200\",\"available\":true,"
            "\"url\":\"http://web.archive.org/web/20260106073238/"
            "https://roaster.example/products/gone\"}}}");
        server.respondForPathWithStatus("/products/gone", "404 Not Found", "gone");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy archivedSpy(&client, &BeanBaseClient::bagLinkArchived);
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);

        client.validateBagLink("canon-arch-1", server.baseUrl() + "/products/gone");
        QVERIFY(archivedSpy.wait(3000));
        QCOMPARE(archivedSpy.count(), 1);
        QCOMPARE(archivedSpy.first().at(0).toString(), QString("canon-arch-1"));
        QCOMPARE(archivedSpy.first().at(1).toString(),
                 QString("https://web.archive.org/web/20260106073238/"
                         "https://roaster.example/products/gone"));
        // The whole point: a recoverable bag is never stamped dead.
        QCOMPARE(deadSpy.count(), 0);
    }

    // An archive that errors must not be read as "no capture" — that would
    // permanently clear a link over a blip, which is exactly the failure the
    // existing transient-error branch already avoids for the roaster.
    void validateBagLinkSilentWhenArchiveFails() {
        FakeBeanBaseServer server;
        server.respondForPathWithStatus("/wayback/available", "503 Service Unavailable",
                                        "<html>we are down</html>");
        server.respondForPathWithStatus("/products/gone", "404 Not Found", "gone");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy archivedSpy(&client, &BeanBaseClient::bagLinkArchived);
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);

        client.validateBagLink("canon-arch-2", server.baseUrl() + "/products/gone");
        QTest::qWait(800);
        QCOMPARE(archivedSpy.count(), 0);
        QCOMPARE(deadSpy.count(), 0);
    }

    // Recovery is terminal: a link that is already a snapshot is never asked
    // about again, so a snapshot that later goes unreachable cannot cascade
    // into clearing the bag's only remaining URL.
    void archivedLinkIsNeverLookedUpAgain() {
        FakeBeanBaseServer server;
        server.respondForPath("/wayback/available", "{\"archived_snapshots\":{}}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy archivedSpy(&client, &BeanBaseClient::bagLinkArchived);

        client.lookupArchivedLink("canon-arch-3",
                                  "https://web.archive.org/web/20260106073238/https://r.example/p");
        QTest::qWait(300);
        QCOMPARE(archivedSpy.count(), 0);
        QCOMPARE(server.requestCount(), qsizetype(0));
    }

    void lookupArchivedLinkEmitsOnHit() {
        FakeBeanBaseServer server;
        server.respondForPath("/wayback/available",
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"200\",\"available\":true,"
            "\"url\":\"http://web.archive.org/web/20260101000000/https://r.example/p\"}}}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy archivedSpy(&client, &BeanBaseClient::bagLinkArchived);

        client.lookupArchivedLink("canon-arch-4", "https://r.example/p");
        QVERIFY(archivedSpy.wait(3000));
        QCOMPARE(archivedSpy.first().at(1).toString(),
                 QString("https://web.archive.org/web/20260101000000/https://r.example/p"));

        // One query per (id, url) per session.
        client.lookupArchivedLink("canon-arch-4", "https://r.example/p");
        QTest::qWait(200);
        QCOMPARE(archivedSpy.count(), 1);
    }

    // ====================================================
    // Link state for result ordering
    // ====================================================

    void linkStateProbeResolvesLiveArchivedAndNone() {
        FakeBeanBaseServer server;
        server.respondForPath("/wayback/available",
            "{\"archived_snapshots\":{\"closest\":{\"status\":\"200\",\"available\":true,"
            "\"url\":\"http://web.archive.org/web/20260101000000/https://r.example/p\"}}}");
        server.respondForPathWithStatus("/dead-with-capture", "404 Not Found", "gone");
        server.respondForPath("/alive", "ok");

        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy spy(&client, &BeanBaseClient::linkStateResolved);

        // An entry with no URL needs no probe to be judged.
        QCOMPARE(client.linkState(QString()), QString("none"));
        // An archive URL is its own answer.
        QCOMPARE(client.linkState("https://web.archive.org/web/20260101000000/https://r.example/p"),
                 QString("archived"));
        // Unprobed is "unknown", never "none" — a row is not labelled worse
        // than what is known about it.
        QCOMPARE(client.linkState(server.baseUrl() + "/alive"), QString("unknown"));

        client.probeLinkState(server.baseUrl() + "/alive");
        QVERIFY(spy.wait(3000));
        QCOMPARE(client.linkState(server.baseUrl() + "/alive"), QString("live"));

        client.probeLinkState(server.baseUrl() + "/dead-with-capture");
        QTRY_COMPARE_WITH_TIMEOUT(
            client.linkState(server.baseUrl() + "/dead-with-capture"), QString("archived"), 4000);
    }

    void linkStateDeadWithNoCaptureIsNone() {
        FakeBeanBaseServer server;
        server.respondForPath("/wayback/available", "{\"archived_snapshots\":{}}");
        server.respondForPathWithStatus("/dead", "404 Not Found", "gone");

        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        client.probeLinkState(server.baseUrl() + "/dead");
        QTRY_COMPARE_WITH_TIMEOUT(client.linkState(server.baseUrl() + "/dead"),
                                  QString("none"), 4000);
    }

    // A storefront that refuses HEAD is not a dead page.
    void linkStateRetriesWithGetWhenHeadIsRefused() {
        FakeBeanBaseServer server;
        server.respondForPathWithStatus("/no-head", "405 Method Not Allowed", "nope");

        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        // The 405 is served to HEAD and to GET alike by this stub, so what the
        // assertion proves is the retry itself: two requests, and no "none".
        client.probeLinkState(server.baseUrl() + "/no-head");
        QTest::qWait(900);
        QCOMPARE(client.linkState(server.baseUrl() + "/no-head"), QString("unknown"));
        QCOMPARE(server.requestCount(), qsizetype(2));
    }

    void linkStateIsCachedForTheSession() {
        FakeBeanBaseServer server;
        server.respondForPath("/alive", "ok");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(server.baseUrl());
        QSignalSpy spy(&client, &BeanBaseClient::linkStateResolved);

        client.probeLinkState(server.baseUrl() + "/alive");
        QVERIFY(spy.wait(3000));
        const qsizetype after = server.requestCount();

        // Re-running the same search must cost nothing.
        client.probeLinkState(server.baseUrl() + "/alive");
        QTest::qWait(300);
        QCOMPARE(server.requestCount(), after);
        QCOMPARE(spy.count(), 1);
    }

    // ====================================================
    // Bag image resolution: og:image extraction + file cache
    // ====================================================

    void extractOgImageVariants() {
        // Canonical attribute order.
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<html><head><meta property=\"og:image\" content=\"https://cdn.x/a.jpg\"></head></html>"),
            QString("https://cdn.x/a.jpg"));
        // Reversed attribute order.
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<meta content=\"https://cdn.x/b.jpg\" property=\"og:image\"/>"),
            QString("https://cdn.x/b.jpg"));
        // name= instead of property=, single quotes, secure_url variant.
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<meta name='og:image:secure_url' content='https://cdn.x/c.jpg'>"),
            QString("https://cdn.x/c.jpg"));
        // Protocol-relative URL normalized to https.
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<meta property=\"og:image\" content=\"//cdn.x/d.jpg\">"),
            QString("https://cdn.x/d.jpg"));
        // Absent / unrelated meta tags / relative path -> empty.
        QCOMPARE(BeanBaseClient::extractOgImage("<html><meta charset=\"utf-8\"></html>"), QString());
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<meta property=\"og:image\" content=\"/relative.jpg\">"), QString());
        QCOMPARE(BeanBaseClient::extractOgImage(""), QString());
        // Hostile schemes never pass the absolute-http filter.
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<meta property=\"og:image\" content=\"file:///etc/passwd\">"), QString());
        // og:image:width (common real-world tag, listed before og:image) must
        // not match the property anchor — the real image URL still wins.
        QCOMPARE(BeanBaseClient::extractOgImage(
            "<meta property=\"og:image:width\" content=\"1200\">"
            "<meta property=\"og:image\" content=\"https://cdn.x/e.jpg\">"),
            QString("https://cdn.x/e.jpg"));
    }

    void ensureBagImageResolvesAndCaches() {
        // Full chain against the fake server: canonical re-search (no stored
        // link) -> product page -> og:image -> downloaded file in the cache dir.
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/api/canonical_coffee_bags",
            "{\"data\":[{\"id\":\"canon-img-1\",\"name\":\"Milk Blend\","
            "\"canonical_roaster_name\":\"Prodigal\",\"url\":\"" + base + "/product\"}]}");
        server.respondForPath("/product",
            "<html><head><meta property=\"og:image\" content=\"" + base + "/photo.jpg\"></head></html>");
        server.respondForPath("/photo.jpg", pngBody("JPEGBYTES"));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        QCOMPARE(client.bagImagePath("canon-img-1"), QString());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-img-1", "Milk Blend", "");
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.first().at(0).toString(), QString("canon-img-1"));
        const QString path = spy.first().at(1).toString();
        QVERIFY(QFile::exists(path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray(pngBody("JPEGBYTES")));
        QCOMPARE(client.bagImagePath("canon-img-1"), path);

        // Cached: a second ensure re-emits (deferred) with the same payload
        // and without any new request.
        const qsizetype requestsAfterResolve = server.requestCount();
        QSignalSpy spy2(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-img-1", "Milk Blend", "");
        QVERIFY(spy2.wait(1000));
        QCOMPARE(spy2.first().at(0).toString(), QString("canon-img-1"));
        QCOMPARE(spy2.first().at(1).toString(), path);
        QCOMPARE(server.requestCount(), requestsAfterResolve);
    }

    void ensureBagImageDirectUrlSkipsResearch() {
        // The primary production path: a blob that already carries `link` goes
        // straight to the product page — the canonical re-search must not run.
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/product",
            "<html><meta property=\"og:image\" content=\"" + base + "/photo.jpg\"></html>");
        server.respondForPath("/photo.jpg", pngBody("DIRECTBYTES"));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-img-2", "Milk Blend", server.baseUrl() + "/product");
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.first().at(0).toString(), QString("canon-img-2"));
        for (const QString& line : server.requestLines())
            QVERIFY2(!line.contains("/api/canonical_coffee_bags"),
                     "direct productUrl must skip the canonical re-search");
    }

    void refreshBagImageReresolvesFromTheNewUrl() {
        // The user edited the bag's product URL. ensureBagImage() alone would
        // short-circuit twice over — the cached file exists AND the id is in
        // the once-per-session attempt guard — and confirm the "refresh" with
        // the OLD page's pixels, silently and forever. refreshBagImage() has to
        // defeat both: it clears the guard and forces past the cache hit, then
        // the new bytes replace the file atomically (it does NOT evict first —
        // see refreshBagImageKeepsTheOldPhotoWhenTheNewPageHasNone for why).
        // Nothing about that failure is observable at runtime:
        // bagImageReady still fires and the UI still updates, just with the
        // wrong roaster's photo, which is why it is pinned here.
        //
        // Keyed "bag-<rowid>" — the manual-bag cache key, which the web
        // /beans editor now uses when it refreshes a URL it just changed.
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/product-old",
            "<html><meta property=\"og:image\" content=\"" + base + "/old.jpg\"></html>");
        server.respondForPath("/old.jpg", pngBody("OLDBYTES"));
        server.respondForPath("/product-new",
            "<html><meta property=\"og:image\" content=\"" + base + "/new.jpg\"></html>");
        server.respondForPath("/new.jpg", pngBody("NEWBYTES"));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        auto cachedBytes = [&client]() {
            QFile f(client.bagImagePath(QStringLiteral("bag-42")));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };

        QSignalSpy first(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("bag-42", "Milk Blend", server.baseUrl() + "/product-old");
        QVERIFY(first.wait(5000));
        QCOMPARE(cachedBytes(), QByteArray(pngBody("OLDBYTES")));

        QSignalSpy refreshed(&client, &BeanBaseClient::bagImageReady);
        client.refreshBagImage("bag-42", "Milk Blend", server.baseUrl() + "/product-new");
        QVERIFY(refreshed.wait(5000));
        QCOMPARE(refreshed.first().at(0).toString(), QString("bag-42"));
        QCOMPARE(cachedBytes(), QByteArray(pngBody("NEWBYTES")));
    }

    void replaceBagImageFromUrlOverwritesWhereCacheFromUrlDeclines() {
        // The pair differs by exactly one guard, and that guard is the whole
        // point of having two: cacheBagImageFromUrl lets an existing photo win
        // (right when warming a bag that has none), replaceBagImageFromUrl does
        // not (right when the user just changed the product URL and the stage-2
        // photo is the only one that page will give up). Pinned together so a
        // future edit cannot quietly collapse them into one behaviour.
        FakeBeanBaseServer server;
        server.respondForPath("/first.jpg", pngBody("FIRSTBYTES"));
        server.respondForPath("/second.jpg", pngBody("SECONDBYTES"));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        auto cachedBytes = [&client]() {
            QFile f(client.bagImagePath(QStringLiteral("bag-77")));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };

        QSignalSpy first(&client, &BeanBaseClient::bagImageReady);
        client.cacheBagImageFromUrl("bag-77", server.baseUrl() + "/first.jpg");
        QVERIFY(first.wait(5000));
        QCOMPARE(cachedBytes(), QByteArray(pngBody("FIRSTBYTES")));

        // cacheBagImageFromUrl declines: the entry already exists, no request.
        const qsizetype afterFirst = server.requestCount();
        client.cacheBagImageFromUrl("bag-77", server.baseUrl() + "/second.jpg");
        QTest::qWait(200);
        QCOMPARE(server.requestCount(), afterFirst);
        QCOMPARE(cachedBytes(), QByteArray(pngBody("FIRSTBYTES")));

        // replaceBagImageFromUrl overwrites it.
        QSignalSpy replaced(&client, &BeanBaseClient::bagImageReady);
        client.replaceBagImageFromUrl("bag-77", server.baseUrl() + "/second.jpg");
        QVERIFY(replaced.wait(5000));
        QCOMPARE(cachedBytes(), QByteArray(pngBody("SECONDBYTES")));

        // And it refuses a traversal-shaped key like every sibling entry point.
        client.replaceBagImageFromUrl("../escape", server.baseUrl() + "/second.jpg");
        QTest::qWait(100);
        QCOMPARE(client.bagImagePath(QStringLiteral("../escape")), QString());
    }

    void refreshBagImageKeepsTheOldPhotoWhenTheNewPageHasNone() {
        // Resolve-then-swap: the cached file must survive a refresh that
        // resolves nothing. Evicting up front made a failed refresh blank the
        // bag permanently, and even a SUCCESSFUL one blanked it for the length
        // of a round trip — which the web grid, reloading the moment the save
        // returns, renders as "editing the URL deleted my photo".
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/product-old",
            "<html><meta property=\"og:image\" content=\"" + base + "/old.jpg\"></html>");
        server.respondForPath("/old.jpg", pngBody("OLDBYTES"));
        server.respondForPath("/product-bare", "<html><body>no og:image here</body></html>");

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy first(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("bag-43", "Milk Blend", server.baseUrl() + "/product-old");
        QVERIFY(first.wait(5000));
        const QString path = client.bagImagePath(QStringLiteral("bag-43"));
        QVERIFY(!path.isEmpty());

        // Refresh against a page with nothing to offer: silent by design, so
        // wait for the request to land rather than for a signal.
        const qsizetype before = server.requestCount();
        client.refreshBagImage("bag-43", "Milk Blend", server.baseUrl() + "/product-bare");
        QTRY_VERIFY_WITH_TIMEOUT(server.requestCount() > before, 5000);
        QTest::qWait(200);

        QVERIFY2(QFile::exists(path), "a refresh that resolves nothing must not blank the bag");
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray(pngBody("OLDBYTES")));
        QCOMPARE(client.bagImagePath(QStringLiteral("bag-43")), path);
    }

    void recoverBagLinkIndependentOfImageCache() {
        // A legacy blob whose photo was cached before link backfill existed:
        // ensureBagImage short-circuits on the file, but the reorder URL must
        // still be recoverable — and recovery must not re-download the image.
        FakeBeanBaseServer server;
        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("/api/canonical_coffee_bags",
            "{\"data\":[{\"id\":\"canon-img-3\",\"name\":\"Milk Blend\","
            "\"canonical_roaster_name\":\"Prodigal\",\"url\":\"" + base + "/product\"}]}");

        QTemporaryDir cacheDir;
        QFile seeded(cacheDir.path() + "/canon-img-3");
        QVERIFY(seeded.open(QIODevice::WriteOnly));
        seeded.write("CACHED");
        seeded.close();

        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        // Image path short-circuits on the seeded file — no network.
        QSignalSpy imgSpy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-img-3", "Milk Blend", "");
        QVERIFY(imgSpy.wait(1000));
        QCOMPARE(server.requestCount(), 0);

        // Link recovery still runs and announces the URL, without touching
        // the product page (no pending image wants it).
        QSignalSpy linkSpy(&client, &BeanBaseClient::bagLinkRecovered);
        client.recoverBagLink("canon-img-3", "Milk Blend");
        QVERIFY(linkSpy.wait(3000));
        QCOMPARE(linkSpy.first().at(0).toString(), QString("canon-img-3"));
        QCOMPARE(linkSpy.first().at(1).toString(), QString(base + "/product"));
        QCOMPARE(server.requestCount(), 1);  // the search only

        // Dedup: second recovery attempt is a no-op.
        client.recoverBagLink("canon-img-3", "Milk Blend");
        QVERIFY(!linkSpy.wait(300) || linkSpy.count() == 1);
        QCOMPARE(server.requestCount(), 1);
    }

    // A snapshot page is fetched in its `id_` form, whose og:image still names
    // the roaster's own asset — so the preference for the original over the
    // archive proxy needs no logic in the image chain, it falls out of which
    // form is fetched.
    void bagImageFromSnapshotFetchesRawFormAndOriginalAsset() {
        FakeBeanBaseServer server;
        const QString host = QStringLiteral("127.0.0.1:%1").arg(server.baseUrl().section(':', -1));
        BeanBaseClient::setArchiveSnapshotHost(host);
        auto restore = qScopeGuard([]() {
            BeanBaseClient::setArchiveSnapshotHost(QStringLiteral("web.archive.org"));
        });

        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("id_/",
            "<html><meta property=\"og:image\" content=\"" + base + "/original.png\"></html>");
        server.respondForPath("/original.png", pngBody("ORIGINALBYTES"));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-snap-1", "Buenos Aires",
                              server.baseUrl() + "/web/20260106073238/https://r.example/p");
        QVERIFY(spy.wait(5000));

        QFile cached(cacheDir.path() + "/canon-snap-1");
        QVERIFY(cached.open(QIODevice::ReadOnly));
        QCOMPARE(cached.readAll(), QByteArray(pngBody("ORIGINALBYTES")));

        bool sawRawForm = false;
        for (const QString& line : server.requestLines())
            if (line.contains("20260106073238id_/"))
                sawRawForm = true;
        QVERIFY2(sawRawForm, "the snapshot page must be fetched in its id_ form");
    }

    // The roaster's asset host usually outlives the product page, but not
    // always. When the original is gone the archive's own copy stands in.
    void bagImageFallsBackToArchivedAsset() {
        FakeBeanBaseServer server;
        const QString host = QStringLiteral("127.0.0.1:%1").arg(server.baseUrl().section(':', -1));
        BeanBaseClient::setArchiveSnapshotHost(host);
        auto restore = qScopeGuard([]() {
            BeanBaseClient::setArchiveSnapshotHost(QStringLiteral("web.archive.org"));
        });

        const QByteArray base = server.baseUrl().toUtf8();
        // Order matters: the archived-asset route is registered first, because
        // its request line also contains the original asset's path.
        server.respondForPath("im_/", pngBody("ARCHIVEDBYTES"));
        server.respondForPath("id_/",
            "<html><meta property=\"og:image\" content=\"" + base + "/original-gone.png\"></html>");
        server.respondForPathWithStatus("/original-gone.png", "404 Not Found", "gone");

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-snap-2", "Buenos Aires",
                              server.baseUrl() + "/web/20260106073238/https://r.example/p");
        QVERIFY(spy.wait(5000));

        QFile cached(cacheDir.path() + "/canon-snap-2");
        QVERIFY(cached.open(QIODevice::ReadOnly));
        QCOMPARE(cached.readAll(), QByteArray(pngBody("ARCHIVEDBYTES")));
    }

    // The archived route reuses downloadBagImage, so the cache's own rules
    // still bind: an oversized asset is refused whichever host served it.
    void archivedAssetStillObeysTheSizeCap() {
        FakeBeanBaseServer server;
        const QString host = QStringLiteral("127.0.0.1:%1").arg(server.baseUrl().section(':', -1));
        BeanBaseClient::setArchiveSnapshotHost(host);
        auto restore = qScopeGuard([]() {
            BeanBaseClient::setArchiveSnapshotHost(QStringLiteral("web.archive.org"));
        });

        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("id_/",
            "<html><meta property=\"og:image\" content=\"" + base + "/huge.png\"></html>");
        server.respondForPath("/huge.png", pngBody(QByteArray(9 * 1024 * 1024, 'x')));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-snap-3", "Buenos Aires",
                              server.baseUrl() + "/web/20260106073238/https://r.example/p");
        QTest::qWait(1500);
        QCOMPARE(spy.count(), 0);
        QVERIFY(!QFile::exists(cacheDir.path() + "/canon-snap-3"));
    }

    // The AI rungs are additive: with no provider anywhere near this, a live
    // link and a dead-link-with-capture must BOTH still produce a photo. The
    // artwork path is a plain fetch and must never become dependent on the AI
    // work layered above it.
    void artworkResolvesWithNoAiInvolved() {
        FakeBeanBaseServer server;
        const QString host = QStringLiteral("127.0.0.1:%1").arg(server.baseUrl().section(':', -1));
        BeanBaseClient::setArchiveSnapshotHost(host);
        auto restore = qScopeGuard([]() {
            BeanBaseClient::setArchiveSnapshotHost(QStringLiteral("web.archive.org"));
        });

        const QByteArray base = server.baseUrl().toUtf8();
        server.respondForPath("id_/",
            "<html><meta property=\"og:image\" content=\"" + base + "/snap.png\"></html>");
        server.respondForPath("/snap.png", pngBody("SNAPBYTES"));
        server.respondForPath("/live-product",
            "<html><meta property=\"og:image\" content=\"" + base + "/live.png\"></html>");
        server.respondForPath("/live.png", pngBody("LIVEBYTES"));

        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("canon-noai-1", "X", server.baseUrl() + "/live-product");
        QVERIFY(spy.wait(5000));
        client.ensureBagImage("canon-noai-2", "X",
                              server.baseUrl() + "/web/20260106073238/https://r.example/p");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(cacheDir.path() + "/canon-noai-2"), 5000);

        QFile live(cacheDir.path() + "/canon-noai-1");
        QVERIFY(live.open(QIODevice::ReadOnly));
        QCOMPARE(live.readAll(), QByteArray(pngBody("LIVEBYTES")));
        QFile snap(cacheDir.path() + "/canon-noai-2");
        QVERIFY(snap.open(QIODevice::ReadOnly));
        QCOMPARE(snap.readAll(), QByteArray(pngBody("SNAPBYTES")));
    }

    void ensureBagImageRejectsUnsafeIds() {
        // The canonical id doubles as the cache filename and round-trips
        // through blobs/backups/migration — traversal-shaped ids are refused
        // before any path use or network activity.
        FakeBeanBaseServer server;
        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        QCOMPARE(client.bagImagePath("../escape"), QString());
        QCOMPARE(client.bagImagePath("a/b"), QString());
        QCOMPARE(client.bagImagePath("a\\b"), QString());

        // The rejection warns rather than bailing mutely: the caller is waiting
        // for a bagImageReady that will never arrive, and a traversal-shaped id
        // means something upstream is corrupt. ignoreMessage doubles as the
        // assertion that it is logged — the test fails if it stops being.
        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        QTest::ignoreMessage(QtWarningMsg,
            "[BeanBase][Image] Refusing unsafe cache key ../escape");
        client.ensureBagImage("../escape", "Nope", server.baseUrl() + "/product");
        QTest::ignoreMessage(QtWarningMsg,
            "[BeanBase][Image] Refusing unsafe cache key a/b");
        client.ensureBagImage("a/b", "Nope", "");
        QVERIFY(!spy.wait(300));
        QCOMPARE(server.requestCount(), 0);
    }

    void ensureBagImageFailureIsSilentAndOnce() {
        // No matching canonical entry -> no signal; the per-session attempt
        // guard keeps a failed id from re-querying on every view.
        FakeBeanBaseServer server;
        server.respondWith("200 OK", "{\"data\":[]}");
        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());

        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("missing-1", "Nope", "");
        QVERIFY(!spy.wait(500));
        const qsizetype requests = server.requestCount();
        QCOMPARE(requests, 1);
        client.ensureBagImage("missing-1", "Nope", "");
        QVERIFY(!spy.wait(300));
        QCOMPARE(server.requestCount(), requests);  // dedup: no second attempt
    }

    void errorPathsSurfaceSearchFailed() {
        // The migration's two new error branches, driven through the network
        // seam: HTTP 429 (the new rate limit) — and any non-200 — must surface
        // searchFailed("network"), never an empty success; a 200 with a
        // non-JSON body must surface searchFailed("parse") and not cache.
        {
            FakeBeanBaseServer server;
            server.respondWith("429 Too Many Requests", "{\"data\":[]}");
            BeanBaseClient client(&m_nam, &m_settings);
            client.setVisualizerBaseUrl(server.baseUrl());
            QSignalSpy ok(&client, &BeanBaseClient::searchResults);
            QSignalSpy fail(&client, &BeanBaseClient::searchFailed);
            client.search("ethiopia");
            QVERIFY(fail.wait(3000));
            QCOMPARE(fail.first().at(1).toString(), QString("network"));
            QCOMPARE(ok.count(), 0);  // throttled response is NOT "no matches"
        }
        {
            FakeBeanBaseServer server;
            server.respondWith("200 OK", "not json at all");
            BeanBaseClient client(&m_nam, &m_settings);
            client.setVisualizerBaseUrl(server.baseUrl());
            QSignalSpy ok(&client, &BeanBaseClient::searchResults);
            QSignalSpy fail(&client, &BeanBaseClient::searchFailed);
            client.search("ethiopia");
            QVERIFY(fail.wait(3000));
            QCOMPARE(fail.first().at(1).toString(), QString("parse"));
            QCOMPARE(ok.count(), 0);  // junk body must not emit/cache a result
        }
    }

    void inFlightSupersedeEmitsSingleTerminalSignal() {
        // A query that reached the wire is superseded by a distinct query while
        // its reply is still in flight. The displaced query must get EXACTLY ONE
        // terminal signal ("superseded") — not "superseded" followed by a
        // spurious "network" from abort()'s synchronous finished().
        FakeBeanBaseServer server;
        server.hangWithoutResponding();  // keep reply A in flight
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        QSignalSpy fail(&client, &BeanBaseClient::searchFailed);

        // Counts terminal signals for the DISPLACED query only. Declared here
        // because both the wait below and the final assertion need it.
        const auto ethiopiaTerminals = [&fail]() {
            int n = 0;
            for (const QList<QVariant>& sig : fail)
                if (sig.at(0).toString() == QString("ethiopia")) ++n;
            return n;
        };

        client.search("ethiopia");
        // Wait for the CONDITION (A reached the wire), not for a fixed slice of
        // time. The server hangs deliberately, so there is no signal to spy on
        // for this step — polling requestCount is the observable. A bare
        // qWait(600) here was a timer standing in for an event: it held on an
        // idle machine and failed under a loaded parallel suite, where the
        // 350 ms debounce plus scheduling ran past the window and this read 0.
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), qsizetype(1), 5000);

        client.search("colombia");            // distinct query supersedes A
        // Again the condition, not a duration: the displaced query's terminal
        // signal is the thing being waited for.
        QTRY_VERIFY_WITH_TIMEOUT(ethiopiaTerminals() >= 1, 5000);

        // Proving the ABSENCE of a second signal does need a settle window —
        // that is inherent to a negative, unlike the two waits above. Short and
        // explicit: abort()'s spurious finished() would land synchronously,
        // long before this elapses.
        QTest::qWait(200);

        QString status;
        for (const QList<QVariant>& sig : fail)
            if (sig.at(0).toString() == QString("ethiopia"))
                status = sig.at(1).toString();

        QCOMPARE(ethiopiaTerminals(), 1);     // abort stayed silent
        QCOMPARE(status, QString("superseded"));
    }

    // ====================================================
    // Single-call enrichment, gather bridge, blob helpers
    // ====================================================

    void fetchCanonicalDetailsFromEntryNoNetwork() {
        // The search entry already carries the descriptive blob (single-call
        // API), so enrichment re-emits it locally (deferred) — zero requests.
        FakeBeanBaseServer server;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        QVariantMap entry;
        entry["id"] = "bag-uuid-1";
        entry["roasterName"] = "Prodigal Coffee";
        entry["roastName"] = "Milk Blend";
        entry["canonicalRoasterId"] = "roaster-uuid-1";
        entry["degree"] = "Light";
        entry["origin"] = "Brazil";
        entry["process"] = "Natural";

        QSignalSpy spy(&client, &BeanBaseClient::canonicalDetails);
        client.fetchCanonicalDetails(entry);
        QVERIFY(spy.wait(2000));  // delivered async (deferred), no network
        QCOMPARE(spy.first().at(0).toString(), QString("bag-uuid-1"));
        const QVariantMap attrs = spy.first().at(1).toMap();
        QCOMPARE(attrs["degree"].toString(), QString("Light"));     // roast_level remapped
        QCOMPARE(attrs["origin"].toString(), QString("Brazil"));    // country remapped
        QCOMPARE(attrs["process"].toString(), QString("Natural"));  // processing remapped
        QCOMPARE(attrs["canonicalRoasterId"].toString(), QString("roaster-uuid-1"));
        QCOMPARE(server.requestCount(), 0);  // no enrichment round-trip

        // An entry with no descriptive values emits nothing (gather grace covers).
        QVariantMap bare;
        bare["id"] = "bare-1";
        bare["canonicalRoasterId"] = "roaster-uuid-1";
        client.fetchCanonicalDetails(bare);
        QTest::qWait(300);
        QCOMPARE(spy.count(), 1);  // still just the first emit
    }

    void beanSearchToolRespondsViaGraceWhenEnrichmentStalls() {
        // A canonical result with NO descriptive fields means enrichment emits
        // nothing — the tool must still respond, via the 4 s enrichment grace
        // window, with identity-only results. Worst failure mode (hang) pinned
        // to "responds with identity-only results".
        FakeBeanBaseServer server;
        server.respondWith("200 OK",
            "{\"data\":[{\"id\":\"abc-123\",\"canonical_roaster_id\":\"r1\","
            "\"canonical_roaster_name\":\"Prodigal Coffee\",\"name\":\"Milk Blend\"}]}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        McpToolRegistry registry;
        registerBeanSearchTool(&registry, &client);

        QString error;
        QJsonObject result;
        bool responded = false;
        registry.callAsyncTool("bean_search", QJsonObject{{"query", "milk blend"}}, 2, error,
            [&](const QJsonObject& r) { result = r; responded = true; });
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QElapsedTimer timer; timer.start();
        while (!responded && timer.elapsed() < 8000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(responded);
        QCOMPARE(result["count"].toInt(), 1);
        QCOMPARE(result["results"].toArray().first().toObject()["id"].toString(),
                 QString("abc-123"));
    }

    void beanSearchToolReportsSupersededInsteadOfHanging() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK", "{\"data\":[]}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        McpToolRegistry registry;
        registerBeanSearchTool(&registry, &client);

        QString error;
        QJsonObject result;
        bool responded = false;
        registry.callAsyncTool("bean_search", QJsonObject{{"query", "ethiopia"}}, 2, error,
            [&](const QJsonObject& r) { result = r; responded = true; });
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // A concurrent consumer (the Beans-page bar) displaces the debounced
        // query before it is ever sent — the tool must NOT hang.
        client.search("colombia");

        QElapsedTimer timer; timer.start();
        while (!responded && timer.elapsed() < 8000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(responded);
        QCOMPARE(result["status"].toString(), QString("superseded"));
    }

    void blobHelpersDefineLinkedAndCanonicalId() {
        // BeanBaseBlob is THE definition of "linked" / "has canonical id" —
        // the uploader's emit-only contract rides on canonicalId() == "".
        QVERIFY(!BeanBaseBlob::isLinked(QString()));
        QVERIFY(!BeanBaseBlob::isLinked("not json"));
        QVERIFY(!BeanBaseBlob::isLinked("{\"origin\":\"Colombia\"}"));   // no id
        QVERIFY(BeanBaseBlob::isLinked("{\"id\":\"abc\"}"));
        QVERIFY(BeanBaseBlob::isLinked("{\"id\":31754}"));                 // numeric id

        QCOMPARE(BeanBaseBlob::canonicalId(QString()), QString());
        QCOMPARE(BeanBaseBlob::canonicalId("garbage"), QString());
        QCOMPARE(BeanBaseBlob::canonicalId("{\"id\":\"31754\"}"), QString());  // Bean Base source: no canonical
        QCOMPARE(BeanBaseBlob::canonicalId(
            "{\"id\":\"abc\",\"visualizerCanonicalId\":\"abc\"}"), QString("abc"));
    }

    // ==========================================
    // Blob edit merge / canonical snapshot / revert (add-bag-detail-editing)
    // ==========================================

    static QJsonObject parsed(const QString& blob) {
        return QJsonDocument::fromJson(blob.toUtf8()).object();
    }

    void mergeCapturesCanonicalOnFirstEditAndPreservesLinkKeys() {
        // A linked blob as a canonical pick stores it: id + identity + details.
        QJsonObject start;
        start["id"] = "uuid-1";
        start["visualizerCanonicalId"] = "uuid-1";
        start["canonicalRoasterId"] = "roaster-uuid";
        start["roasterName"] = "Prodigal";
        start["roastName"] = "First Batch";
        start["origin"] = "Colombia";
        start["tastingNotes"] = "cherry";
        start["description"] = "catalog text";
        const QString blob = QString::fromUtf8(QJsonDocument(start).toJson(QJsonDocument::Compact));

        const QString merged = BeanBaseBlob::mergeBeanDetails(
            blob, {{"tastingNotes", "plum, cocoa"}, {"link", "https://example.com/bag"}});
        const QJsonObject obj = parsed(merged);

        // Link keys + non-edited fields preserved; edits applied.
        QCOMPARE(obj.value("id").toString(), QString("uuid-1"));
        QCOMPARE(obj.value("canonicalRoasterId").toString(), QString("roaster-uuid"));
        QCOMPARE(obj.value("description").toString(), QString("catalog text"));
        QCOMPARE(obj.value("origin").toString(), QString("Colombia"));
        QCOMPARE(obj.value("tastingNotes").toString(), QString("plum, cocoa"));
        QCOMPARE(obj.value("link").toString(), QString("https://example.com/bag"));

        // First edit captured the PRE-edit values as the pristine snapshot.
        const QJsonObject canonical = obj.value("canonical").toObject();
        QCOMPARE(canonical.value("tastingNotes").toString(), QString("cherry"));
        QCOMPARE(canonical.value("origin").toString(), QString("Colombia"));
        QVERIFY(!canonical.contains("link"));  // canonical had none

        // A second edit leaves the snapshot untouched.
        const QJsonObject again = parsed(BeanBaseBlob::mergeBeanDetails(merged, {{"origin", "Peru"}}));
        QCOMPARE(again.value("origin").toString(), QString("Peru"));
        QCOMPARE(again.value("canonical").toObject().value("origin").toString(), QString("Colombia"));
        QCOMPARE(again.value("canonical").toObject().value("tastingNotes").toString(), QString("cherry"));
    }

    void mergeOnManualBagAddsDetailsWithoutLinking() {
        // Empty blob + manual details: keys land, no id, no snapshot, unlinked.
        const QString merged = BeanBaseBlob::mergeBeanDetails(
            QString(), {{"origin", "Ethiopia"}, {"variety", "Heirloom"}, {"farm", "Gora Kone"},
                        {"qualityScore", "88"}, {"placeOfPurchase", "Local cafe"}});
        const QJsonObject obj = parsed(merged);
        QCOMPARE(obj.value("origin").toString(), QString("Ethiopia"));
        QCOMPARE(obj.value("farm").toString(), QString("Gora Kone"));
        QCOMPARE(obj.value("qualityScore").toString(), QString("88"));
        QCOMPARE(obj.value("placeOfPurchase").toString(), QString("Local cafe"));
        QVERIFY(!obj.contains("canonical"));
        QVERIFY(!BeanBaseBlob::isLinked(merged));
    }

    void mergeEmptyValueRemovesKeyAndClearingAllYieldsEmptyBlob() {
        const QString withDetails = BeanBaseBlob::mergeBeanDetails(
            QString(), {{"origin", "Ethiopia"}, {"region", "Guji"}});
        const QString cleared = BeanBaseBlob::mergeBeanDetails(withDetails, {{"region", "  "}});
        QVERIFY(!parsed(cleared).contains("region"));
        QCOMPARE(parsed(cleared).value("origin").toString(), QString("Ethiopia"));

        // Clearing the last key returns "" — the zero-footprint empty blob.
        QCOMPARE(BeanBaseBlob::mergeBeanDetails(cleared, {{"origin", ""}}), QString());
        // Non-editable keys in the edits map are ignored entirely.
        const QJsonObject obj = parsed(BeanBaseBlob::mergeBeanDetails(
            withDetails, {{"id", "forged"}, {"canonical", "forged"}}));
        QVERIFY(!obj.contains("id"));
        QVERIFY(!obj.contains("canonical"));
    }

    // The apply rule, one row of the spec's table per assertion. The blob
    // already knows which values came from Bean Base: a flat value equal to its
    // canonical counterpart did, one that differs was typed by the user.
    void extractionFillsEmptyAndCorrectsCanonicalOnly() {
        const QString blob = "{\"id\":\"canon-1\",\"process\":\"Natural\",\"variety\":\"Bourbon\","
                             "\"canonical\":{\"process\":\"Natural\",\"variety\":\"Caturra\"}}";
        const auto out = BeanBaseBlob::applyExtraction(
            blob, QVariantMap{{"process", "Washed"},     // canonical-sourced -> corrected
                              {"variety", "Gesha"},      // user-edited       -> kept
                              {"origin", "Colombia"}});  // empty             -> filled
        const QJsonObject obj = QJsonDocument::fromJson(out.blob.toUtf8()).object();
        QCOMPARE(obj.value("process").toString(), QString("Washed"));
        QCOMPARE(obj.value("variety").toString(), QString("Bourbon"));
        QCOMPARE(obj.value("origin").toString(), QString("Colombia"));

        // Filling is not correcting: only the overwrite is reported.
        QCOMPARE(out.corrections.size(), qsizetype(1));
        const QVariantMap correction = out.corrections.first().toMap();
        QCOMPARE(correction.value("field").toString(), QString("process"));
        QCOMPARE(correction.value("from").toString(), QString("Natural"));
        QCOMPARE(correction.value("to").toString(), QString("Washed"));

        // The pristine snapshot is untouched, so Revert still undoes the
        // correction exactly as it undoes a manual edit.
        QCOMPARE(obj.value("canonical").toObject().value("process").toString(), QString("Natural"));
        const QJsonObject reverted =
            QJsonDocument::fromJson(BeanBaseBlob::revertToCanonical(out.blob).toUtf8()).object();
        QCOMPARE(reverted.value("process").toString(), QString("Natural"));
    }

    // A linked blob that has never been edited carries no `canonical` yet, and
    // its flat values ARE Bean Base's by construction. The snapshot must be
    // captured before anything is corrected, or Revert would restore the
    // page's values as though Bean Base had said them.
    void extractionSnapshotsBeforeCorrectingAnUneditedBlob() {
        const QString blob = "{\"id\":\"canon-2\",\"process\":\"Natural\"}";
        const auto out = BeanBaseBlob::applyExtraction(blob, QVariantMap{{"process", "Washed"}});
        const QJsonObject obj = QJsonDocument::fromJson(out.blob.toUtf8()).object();
        QCOMPARE(obj.value("process").toString(), QString("Washed"));
        QCOMPARE(obj.value("canonical").toObject().value("process").toString(), QString("Natural"));
        QCOMPARE(out.corrections.size(), qsizetype(1));
    }

    // A manual bag has no canonical entry at all, so every value in it is the
    // user's. The page fills the gaps and touches nothing else.
    void extractionNeverOverwritesAManualBag() {
        const QString blob = "{\"process\":\"Natural\"}";
        const auto out = BeanBaseBlob::applyExtraction(
            blob, QVariantMap{{"process", "Washed"}, {"origin", "Peru"}});
        const QJsonObject obj = QJsonDocument::fromJson(out.blob.toUtf8()).object();
        QCOMPARE(obj.value("process").toString(), QString("Natural"));
        QCOMPARE(obj.value("origin").toString(), QString("Peru"));
        QVERIFY(out.corrections.isEmpty());
    }

    // The caller's live values win over the blob's where it has them — the bag
    // editor's form is the working copy while the dialog is open.
    void extractionJudgesTheCallersLiveValues() {
        const QString blob = "{\"id\":\"canon-3\",\"process\":\"Natural\","
                             "\"canonical\":{\"process\":\"Natural\"}}";
        // The user typed over it in the form but has not saved.
        const auto out = BeanBaseBlob::applyExtraction(
            blob, QVariantMap{{"process", "Washed"}}, QVariantMap{{"process", "Anaerobic"}});
        QVERIFY(out.applied.isEmpty());
        QVERIFY(out.corrections.isEmpty());
    }

    // The page stating nothing about a field never clears it, and a page that
    // agrees with the bag changes nothing at all.
    void extractionIgnoresEmptyAndAgreeingValues() {
        const QString blob = "{\"id\":\"canon-4\",\"process\":\"Washed\","
                             "\"canonical\":{\"process\":\"Washed\"}}";
        const auto out = BeanBaseBlob::applyExtraction(
            blob, QVariantMap{{"process", "Washed"}, {"origin", ""}});
        QVERIFY(out.applied.isEmpty());
        QVERIFY(out.corrections.isEmpty());
        const QJsonObject obj = QJsonDocument::fromJson(out.blob.toUtf8()).object();
        QCOMPARE(obj.value("process").toString(), QString("Washed"));
        QVERIFY(!obj.contains("origin"));
    }

    // A corrupt blob is refused, not rebuilt — the same non-destructive rule
    // mergeBeanDetails follows.
    // The extraction speaks `roastLevel`; the blob and every form call it
    // `degree`. The alias belongs to the shared rule, not to each caller —
    // the MCP surface had no copy and silently dropped the field.
    void extractionAliasesRoastLevelToDegree() {
        const auto out = BeanBaseBlob::applyExtraction(
            "{\"id\":\"canon-5\"}", QVariantMap{{"roastLevel", "Medium-Light"}});
        QCOMPARE(out.applied.value("degree").toString(), QString("Medium-Light"));
        const QJsonObject obj = QJsonDocument::fromJson(out.blob.toUtf8()).object();
        QCOMPARE(obj.value("degree").toString(), QString("Medium-Light"));
    }

    void extractionRefusesACorruptBlob() {
        QTest::ignoreMessage(QtWarningMsg,
            "[BeanBase][Blob] Refusing extraction into corrupt blob (kept unchanged)");
        const auto out = BeanBaseBlob::applyExtraction("not json", QVariantMap{{"origin", "Peru"}});
        QCOMPARE(out.blob, QString("not json"));
        QVERIFY(out.applied.isEmpty());
    }

    void revertRestoresCanonicalValuesAndRemovesUserAdditions() {
        QJsonObject start;
        start["id"] = "uuid-1";
        start["roastName"] = "First Batch";
        start["origin"] = "Colombia";
        const QString blob = QString::fromUtf8(QJsonDocument(start).toJson(QJsonDocument::Compact));
        const QString edited = BeanBaseBlob::mergeBeanDetails(
            blob, {{"roastName", "First Batch 2026"}, {"origin", "Peru"},
                   {"link", "https://example.com/added"}});
        QVERIFY(BeanBaseBlob::differsFromCanonical(edited));

        const QString reverted = BeanBaseBlob::revertToCanonical(edited);
        const QJsonObject obj = parsed(reverted);
        QCOMPARE(obj.value("roastName").toString(), QString("First Batch"));
        QCOMPARE(obj.value("origin").toString(), QString("Colombia"));
        QVERIFY(!obj.contains("link"));  // user addition canonical lacked: removed
        QCOMPARE(obj.value("id").toString(), QString("uuid-1"));
        QVERIFY(!BeanBaseBlob::differsFromCanonical(reverted));
    }

    void corruptBlobIsNeverDestructivelyRebuilt() {
        // A non-empty blob that doesn't parse to a JSON object (truncated
        // write, damaged DB) must survive merge/revert UNCHANGED — rebuilding
        // it from the edits alone would silently discard the canonical link,
        // snapshot, and description while beanbase_id still claims a link.
        const QString truncated = "{\"id\":\"uuid-1\",\"origin\":\"Colo";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing merge into"));
        QCOMPARE(BeanBaseBlob::mergeBeanDetails(truncated, {{"origin", "Peru"}}), truncated);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing revert of"));
        QCOMPARE(BeanBaseBlob::revertToCanonical(truncated), truncated);
        QVERIFY(!BeanBaseBlob::differsFromCanonical(truncated));

        const QString array = "[1,2,3]";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing merge into"));
        QCOMPARE(BeanBaseBlob::mergeBeanDetails(array, {{"origin", "Peru"}}), array);

        // Empty is NOT corrupt — the manual-details path must keep working.
        QVERIFY(!BeanBaseBlob::mergeBeanDetails(QString(), {{"origin", "Peru"}}).isEmpty());
    }

    void numericJsonValuesSurviveCaptureAndDiff() {
        // Bean-Base-era blobs carry numeric values ("id":31754); capture/diff
        // use toVariant().toString() so numbers aren't dropped as empty. A
        // "consistency" refactor to plain toString() would silently break
        // snapshot capture and report spurious diffs — this pins the choice.
        const QString blob = "{\"id\":31754,\"qualityScore\":87,\"origin\":\"Colombia\"}";
        const QString merged = BeanBaseBlob::mergeBeanDetails(blob, {{"origin", "Peru"}});
        const QJsonObject canonical = QJsonDocument::fromJson(merged.toUtf8())
                                          .object().value("canonical").toObject();
        QCOMPARE(canonical.value("qualityScore").toVariant().toString(), QString("87"));
        QVERIFY(BeanBaseBlob::differsFromCanonical(merged));   // origin changed
        QVERIFY(!BeanBaseBlob::differsFromCanonical(BeanBaseBlob::revertToCanonical(merged)));
    }

    void revertRestoresAClearedCanonicalKey() {
        // Third revert direction: the user CLEARED a key the canonical entry
        // supplied (working key absent, snapshot key present).
        const QString blob = "{\"id\":\"uuid-1\",\"region\":\"Huila\"}";
        const QString cleared = BeanBaseBlob::mergeBeanDetails(blob, {{"region", ""}});
        QVERIFY(!parsed(cleared).contains("region"));
        QVERIFY(BeanBaseBlob::differsFromCanonical(cleared));
        QCOMPARE(parsed(BeanBaseBlob::revertToCanonical(cleared)).value("region").toString(),
                 QString("Huila"));
    }

    void extractPageTextStripsAndSquishes() {
        // The "Get info" HTML->text reduction (same as Visualizer's scraper):
        // script/style/svg bodies removed, tags stripped, entities decoded,
        // whitespace squished.
        const QByteArray html =
            "<html><head><style>.a{color:red}</style>"
            "<script>var x = '<div>not text</div>';</script></head>"
            "<body><h1>Saka  Caffe</h1><svg><path d=\"M0 0\"/></svg>"
            "<p>Sweet &amp; creamy,\n\n low acidity &#39;espresso&#39;</p>"
            "<img src=\"x.jpg\"></body></html>";
        const QString text = BeanBaseClient::extractPageText(html);
        QCOMPARE(text, QString("Saka Caffe Sweet & creamy, low acidity 'espresso'"));

        // Length cap: a giant page is truncated, not passed through.
        QByteArray big = "<body>";
        for (int i = 0; i < 5000; i++)
            big += "<p>lorem ipsum dolor sit amet</p>";
        big += "</body>";
        QVERIFY(BeanBaseClient::extractPageText(big).size() <= 48000);
    }

    // --- Get info reaches the archive on its own -------------------------
    //
    // The link check was the only path there and it only runs on a link stored
    // on the bag, so a URL that arrived any other way — restored with the Bean
    // Base data, typed, or accepted from the AI — 404'd and stopped.


    // Two halves, because the seam between them is not reachable from this
    // stub: parseArchiveSnapshot upgrades the capture URL to https (product
    // behaviour, covered by parseArchiveSnapshotVariants) and the stub speaks
    // plain HTTP, so a recovered fetch never lands on it.

    // Half one: a page confirmed gone asks the archive at all, which is what
    // the reported bag never did.
    void getInfoAsksTheArchiveWhenThePageIsGone() {
        ArchiveStub stub;
        stub.server.respondForPathWithStatus("/wayback/available", "200 OK",
                                             "{\"archived_snapshots\":{}}");
        stub.server.respondForPathWithStatus("/dead", "404 Not Found", "gone");

        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(stub.server.baseUrl());
        QSignalSpy failed(&client, &BeanBaseClient::pageTextFailed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unreadable, (no archived copy|archive gave no answer)"));
        client.fetchPageText(stub.server.baseUrl() + "/dead");
        QVERIFY(failed.wait(5000));

        qsizetype asked = 0;
        for (const QString& line : stub.server.requestLines())
            if (line.contains("/wayback/available"))
                asked++;
        QCOMPARE(asked, qsizetype(1));
    }

    // Half two: a snapshot is read in its id_ form, so the AI gets the original
    // page bytes rather than the archive's toolbar markup.
    void getInfoReadsASnapshotInItsRawForm() {
        ArchiveStub stub;
        const QByteArray page = "<html><body><p>" + QByteArray(200, 'a') + "</p></body></html>";
        stub.server.respondForPathWithStatus("id_/", "200 OK", page);

        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy ready(&client, &BeanBaseClient::pageTextReady);
        const QString snapshot =
            stub.server.baseUrl() + "/web/20260106073238/https://r.example/p";
        client.fetchPageText(snapshot);
        QVERIFY(ready.wait(5000));
        // Echoed as the URL the caller asked for: the dialog gates completion
        // on the URL it sent.
        QCOMPARE(ready.last().at(0).toString(), snapshot);
        QVERIFY(ready.last().at(1).toString().contains(QString(200, 'a')));

        bool sawRawForm = false;
        for (const QString& line : stub.server.requestLines())
            if (line.contains("20260106073238id_/"))
                sawRawForm = true;
        QVERIFY2(sawRawForm, "the snapshot must be read in its id_ form");
    }


    // No-capture and archive fault both name the PAGE — see requestPageText.
    void getInfoReportsThePageWhenTheArchiveHasNothingOrFaults() {
        for (const QByteArray& archiveStatus : {QByteArray("200 OK"), QByteArray("429 Too Many Requests")}) {
            ArchiveStub stub;
            stub.server.respondForPathWithStatus("/wayback/available", archiveStatus,
                                                 "{\"archived_snapshots\":{}}");
            stub.server.respondForPathWithStatus("/dead", "404 Not Found", "gone");

            BeanBaseClient client(&m_nam, &m_settings);
            client.setArchiveBaseUrl(stub.server.baseUrl());
            QSignalSpy failed(&client, &BeanBaseClient::pageTextFailed);
            // The USER sees the page's error either way, but the LOG keeps the
            // distinction: "no capture" and "the archive refused to answer"
            // send a reader to different places, and a 429 succeeds on retry.
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
                archiveStatus == QByteArray("200 OK") ? "unreadable, no archived copy"
                                                      : "unreadable, archive gave no answer"));
            client.fetchPageText(stub.server.baseUrl() + "/dead");
            QVERIFY(failed.wait(5000));
            QCOMPARE(failed.last().at(0).toString(), stub.server.baseUrl() + "/dead");
            // The page's own transport error, never "the archive said no" and
            // never a stable code the UI would translate into a page verdict.
            const QString reason = failed.last().at(1).toString();
            QVERIFY(!reason.isEmpty());
            QVERIFY(reason != QStringLiteral("emptyPage"));
            QVERIFY(reason != QStringLiteral("notAWebPage"));
        }
    }

    // One extra fetch, never a chain: a snapshot that is itself gone is the end
    // of the line, not a second archive question.
    // The retry decision, asserted directly. The network form of this was
    // vacuous: parseArchiveSnapshot upgrades the capture URL to https, the stub
    // speaks plain HTTP, so the recovered fetch died on the handshake with
    // status 0 — which short-circuits the gate before archiveFallback is ever
    // read. Flipping the recursion bound to `true` left it green.
    void archiveRetryApplies_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("archiveFallback");
        QTest::addColumn<bool>("expected");
        QTest::newRow("404 asks")                << 404 << true  << true;
        QTest::newRow("410 asks")                << 410 << true  << true;
        QTest::newRow("500 does not")            << 500 << true  << false;
        QTest::newRow("503 does not")            << 503 << true  << false;
        QTest::newRow("403 does not")            << 403 << true  << false;
        QTest::newRow("429 does not")            << 429 << true  << false;
        QTest::newRow("transport does not")      << 0   << true  << false;
        QTest::newRow("404 on a retry does not") << 404 << false << false;
    }
    void archiveRetryApplies() {
        QFETCH(int, status);
        QFETCH(bool, archiveFallback);
        QFETCH(bool, expected);
        QCOMPARE(BeanBaseClient::archiveRetryApplies(status, archiveFallback), expected);
    }

    // A URL that is ALREADY a snapshot has nowhere further to fall back to.
    void getInfoDoesNotAskTheArchiveAboutASnapshot() {
        ArchiveStub stub;
        stub.server.respondWith("404 Not Found", "gone");

        BeanBaseClient client(&m_nam, &m_settings);
        client.setArchiveBaseUrl(stub.server.baseUrl());
        QSignalSpy failed(&client, &BeanBaseClient::pageTextFailed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unreadable -"));
        client.fetchPageText(stub.server.baseUrl() + "/web/20260106073238/https://r.example/p");
        QVERIFY(failed.wait(5000));

        for (const QString& line : stub.server.requestLines())
            QVERIFY2(!line.contains("/wayback/available"),
                     "a snapshot URL must not be looked up in the archive again");
    }

    // --- A changed link is checked again ---------------------------------

    // Marks carried across a rewrite exempt the new url from ever being probed.
    void writingADifferentLinkDropsTheMarksThatDescribedTheOldOne() {
        const QString stamped = QStringLiteral(
            "{\"id\":\"c1\",\"link\":\"https://r.example/old\",\"linkChecked\":true}");

        const QString moved = BeanBaseClient::blobWithLink(stamped, "https://r.example/new");
        const QJsonObject movedObj = QJsonDocument::fromJson(moved.toUtf8()).object();
        QCOMPARE(movedObj.value("link").toString(), QString("https://r.example/new"));
        QVERIFY(!movedObj.contains("linkChecked"));

        // linkDead is the mark that caused the reported bug — it hides Get info
        // and diverts to the search — so assert it separately from linkChecked.
        const QString wasDead = QStringLiteral(
            "{\"id\":\"c1\",\"link\":\"https://r.example/old\",\"linkDead\":true}");
        const QJsonObject revived = QJsonDocument::fromJson(
            BeanBaseClient::blobWithLink(wasDead, "https://r.example/new").toUtf8()).object();
        QVERIFY2(!revived.contains("linkDead"), "a new url does not inherit the old url's verdict");

        // Same value is not a rewrite: re-saving a bag must not re-probe a url
        // that has already been answered for.
        const QString same = BeanBaseClient::blobWithLink(stamped, "https://r.example/old");
        QVERIFY(QJsonDocument::fromJson(same.toUtf8()).object().value("linkChecked").toBool());

        // Clearing the url drops them too — nothing is left describing a url
        // the blob no longer has.
        const QString cleared = BeanBaseClient::blobWithLink(stamped, "");
        const QJsonObject clearedObj = QJsonDocument::fromJson(cleared.toUtf8()).object();
        QVERIFY(!clearedObj.contains("link"));
        QVERIFY(!clearedObj.contains("linkChecked"));
    }

    // The bag editor's save path — the most-travelled of the three writers, and
    // the one a user reaches by typing a replacement URL by hand.
    void savingADifferentLinkDropsTheMarksToo() {
        const QString stamped = QStringLiteral(
            "{\"id\":\"c1\",\"link\":\"https://r.example/old\","
            "\"linkChecked\":true,\"linkDead\":true}");
        const QJsonObject edited = QJsonDocument::fromJson(
            BeanBaseBlob::mergeBeanDetails(
                stamped, {{"link", "https://r.example/typed"}}).toUtf8()).object();
        QCOMPARE(edited.value("link").toString(), QString("https://r.example/typed"));
        QVERIFY(!edited.contains("linkChecked"));
        QVERIFY(!edited.contains("linkDead"));

        // Re-saving the same URL is not a rewrite: the verdict still describes it.
        const QJsonObject resaved = QJsonDocument::fromJson(
            BeanBaseBlob::mergeBeanDetails(
                stamped, {{"link", "https://r.example/old"}}).toUtf8()).object();
        QVERIFY(resaved.value("linkChecked").toBool());
        QVERIFY(resaved.value("linkDead").toBool());
    }

    // A corrupt blob is refused by both link writers, not rebuilt — the same
    // non-destructive rule mergeBeanDetails follows. BagCard reaches these with
    // the blob AS STORED for exactly this reason: a QML JSON.parse failure
    // yields {}, which is valid JSON and would sail past the guard.
    void linkWritersRefuseACorruptBlob() {
        const QString corrupt = QStringLiteral("{not json");
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Refusing a link write into corrupt blob"));
        QCOMPARE(BeanBaseClient::blobWithLink(corrupt, "https://r.example/p"), corrupt);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Refusing a link-verdict write into corrupt blob"));
        QCOMPARE(BeanBaseClient::blobWithLinkVerdict(corrupt, "https://r.example/p", false),
                 corrupt);
    }

    // The verdict writers set the marks setBlobLink drops, so they take the
    // other entry point — and it must actually stamp what it was asked for.
    void blobWithLinkVerdictWritesTheMarksItIsGiven() {
        const QString blob = QStringLiteral("{\"id\":\"c1\",\"link\":\"https://r.example/old\"}");
        const QJsonObject recovered = QJsonDocument::fromJson(
            BeanBaseClient::blobWithLinkVerdict(
                blob, "https://web.archive.org/web/1/https://r.example/old", false).toUtf8()).object();
        QVERIFY(recovered.value("linkChecked").toBool());
        QVERIFY(!recovered.contains("linkDead"));

        const QJsonObject dead = QJsonDocument::fromJson(
            BeanBaseClient::blobWithLinkVerdict(blob, "", true).toUtf8()).object();
        QVERIFY(!dead.contains("link"));
        QVERIFY(dead.value("linkDead").toBool());
        QVERIFY(dead.value("linkChecked").toBool());
    }

    // The path the reported bag actually took.
    void revertingToBeanBaseDataReopensTheLinkCheck() {
        const QString blob = QStringLiteral(
            "{\"id\":\"c1\",\"link\":\"https://r.example/typed\",\"linkChecked\":true,"
            "\"canonical\":{\"link\":\"https://r.example/canonical\"}}");
        const QJsonObject obj =
            QJsonDocument::fromJson(BeanBaseClient::revertToCanonical(blob).toUtf8()).object();
        QCOMPARE(obj.value("link").toString(), QString("https://r.example/canonical"));
        QVERIFY2(!obj.contains("linkChecked"),
                 "a restored url is a different url and must be checked");
    }

    // A DEAD url takes the search's side, not the extraction's.
    void linkIsUsableFollowsTheDeadMarkNotJustEmptiness() {
        QVERIFY(BeanBaseClient::linkIsUsable("{\"id\":\"c1\"}", "https://r.example/p"));
        QVERIFY(!BeanBaseClient::linkIsUsable("{\"id\":\"c1\"}", ""));
        QVERIFY(!BeanBaseClient::linkIsUsable("{\"id\":\"c1\"}", "   "));
        QVERIFY(!BeanBaseClient::linkIsUsable(
            "{\"id\":\"c1\",\"link\":\"https://r.example/p\",\"linkDead\":true}",
            "https://r.example/p"));
        // The mark is about the STORED url, so a url the user typed since does
        // not inherit it — otherwise a bag whose link died could never be given
        // a working one by hand.
        QVERIFY(BeanBaseClient::linkIsUsable("{\"id\":\"c1\",\"linkDead\":true}",
                                             "https://r.example/typed"));
    }

    // Keyed on the bag alone, the guard also refused a DIFFERENT url on that
    // bag — exactly what a revert or an accepted suggestion produces.
    void theLinkCheckGuardIsPerUrlNotPerBag() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK", "{}");
        BeanBaseClient client(&m_nam, &m_settings);

        client.validateBagLink("canon-guard", server.baseUrl() + "/first");
        QTRY_COMPARE(server.requestCount(), qsizetype(1));
        // Same bag, same url: already answered.
        client.validateBagLink("canon-guard", server.baseUrl() + "/first");
        QVERIFY(!QTest::qWaitFor([&]() { return server.requestCount() > 1; }, 300));
        // Same bag, different url: a different question.
        client.validateBagLink("canon-guard", server.baseUrl() + "/second");
        QTRY_COMPARE(server.requestCount(), qsizetype(2));
    }

    void fetchPageTextOutcomes() {
        FakeBeanBaseServer server;
        server.setContentType("text/html");
        QByteArray page = "<html><body><p>" + QByteArray(200, 'a') + "</p></body></html>";
        server.respondForPath("/page", page);
        server.respondForPath("/short", "<html><body>Denied</body></html>");
        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy ready(&client, &BeanBaseClient::pageTextReady);
        QSignalSpy failed(&client, &BeanBaseClient::pageTextFailed);

        // Success: text extracted, URL echoed back (the staleness gate).
        const QString pageUrl = server.baseUrl() + "/page";
        client.fetchPageText(pageUrl);
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.last().at(0).toString(), pageUrl);
        QVERIFY(ready.last().at(1).toString().contains(QString(200, 'a')));

        // Under 100 readable chars = the Visualizer "blocked or empty" gate:
        // a bot wall must be a visible failure, not AI input. Each failure
        // path below intentionally logs a qWarning from the code under test;
        // ignoreMessage consumes them so the suite stays warning-clean.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("yielded no readable text"));
        client.fetchPageText(server.baseUrl() + "/short");
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.last().at(1).toString(), QString("emptyPage"));

        // Non-text content (a PDF/image link) is a FORMAT failure, not a
        // confident "nothing found on the page".
        server.setContentType("application/pdf");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("is not a web page"));
        client.fetchPageText(pageUrl);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.last().at(1).toString(), QString("notAWebPage"));
        server.setContentType("text/html");

        // HTTP errors surface Qt's error string (reply->error() covers 4xx).
        // A 404 now asks the archive first; pointed at the same stub, which
        // 404s the availability request too, that is a fault and the PAGE's
        // error is what reaches the user.
        client.setArchiveBaseUrl(server.baseUrl());
        server.respondWith("404 Not Found", "gone");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("unreadable, (no archived copy|archive gave no answer)"));
        client.fetchPageText(server.baseUrl() + "/nothing-here");
        QVERIFY(failed.wait(5000));
        QVERIFY(!failed.last().at(1).toString().isEmpty());

        // http(s)-only gate: the URL is user-entered and the text is shipped
        // to a third-party AI — file:// must never be read. No server hit.
        const qsizetype requestsBefore = server.requestCount();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Rejected non-http url"));
        client.fetchPageText("file:///etc/hosts");
        QVERIFY(failed.wait(1000));
        QCOMPARE(failed.last().at(1).toString(), QString("invalidUrl"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Rejected non-http url"));
        client.fetchPageText("not a url");
        QVERIFY(failed.wait(1000));
        QCOMPARE(failed.last().at(1).toString(), QString("invalidUrl"));
        QCOMPARE(server.requestCount(), requestsBefore);
    }

    void ensureBagImageManualBagNoUrlIsSilent() {
        // A manual bag's name must never leak to the canonical search API:
        // "bag-" keys have no canonical entry to recover a URL from, so an
        // empty product URL means no image, no network.
        FakeBeanBaseServer server;
        QTemporaryDir cacheDir;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());
        client.setImageCacheDir(cacheDir.path());
        QSignalSpy spy(&client, &BeanBaseClient::bagImageReady);
        client.ensureBagImage("bag-42", "My Home Roast", "");
        QVERIFY(!spy.wait(300));
        QCOMPARE(server.requestCount(), 0);
    }

    void revertAndDiffAreNoopsWithoutLinkOrSnapshot() {
        // Manual bag with details: no snapshot, nothing to revert to.
        const QString manual = BeanBaseBlob::mergeBeanDetails(QString(), {{"origin", "Ethiopia"}});
        QCOMPARE(BeanBaseBlob::revertToCanonical(manual), manual);
        QVERIFY(!BeanBaseBlob::differsFromCanonical(manual));

        // Linked-but-never-edited legacy blob: no snapshot yet, revert no-op.
        const QString legacy = "{\"id\":\"uuid-1\",\"origin\":\"Colombia\"}";
        QCOMPARE(BeanBaseBlob::revertToCanonical(legacy), legacy);
        QVERIFY(!BeanBaseBlob::differsFromCanonical(legacy));

        QCOMPARE(BeanBaseBlob::revertToCanonical(QString()), QString());
        QVERIFY(!BeanBaseBlob::differsFromCanonical(QString()));
    }

    void canonicalIdentityConflictsGuardsTheExportedLink() {
        // The shipped defect: a bag linked to another roaster's record for the
        // same coffee. Exporting the link made visualizer.coffee rewrite
        // bean_brand from the canonical record.
        const QString borrowed = "{\"id\":\"uuid-1\",\"visualizerCanonicalId\":\"uuid-1\","
                                 "\"roasterName\":\"Coava Coffee Roasters\","
                                 "\"roastName\":\"Las Capucas\"}";
        QVERIFY(BeanBaseBlob::canonicalIdentityConflicts(borrowed, {"Stavanger Kaffebrenneri",
                                                          "Las Capucas"}));
        // Genuinely the same coffee: link exports, case and padding ignored.
        QVERIFY(!BeanBaseBlob::canonicalIdentityConflicts(borrowed, {" coava coffee roasters ",
                                                           "Las Capucas"}));
        // A different coffee from the right roaster conflicts too — bean_type is
        // rewritten by the same server callback.
        QVERIFY(BeanBaseBlob::canonicalIdentityConflicts(borrowed, {"Coava Coffee Roasters",
                                                          "Kirinyaga"}));

        // The user's edits live in the working keys, so the pristine `canonical`
        // snapshot decides: renaming the roaster locally is what creates the
        // conflict, and it must be detected from the snapshot, not the edit.
        const QString edited = BeanBaseBlob::mergeBeanDetails(
            borrowed, {{"roasterName", "Stavanger Kaffebrenneri"}});
        QVERIFY(BeanBaseBlob::canonicalIdentityConflicts(edited, {"Stavanger Kaffebrenneri",
                                                        "Las Capucas"}));

        // Unknown proves nothing: legacy blobs stored no names, and an unnamed
        // local bag must keep the link it has today.
        QVERIFY(!BeanBaseBlob::canonicalIdentityConflicts("{\"id\":\"uuid-1\"}", {"Stavanger", "X"}));
        QVERIFY(!BeanBaseBlob::canonicalIdentityConflicts(borrowed, {"", ""}));
        QVERIFY(!BeanBaseBlob::canonicalIdentityConflicts(QString(), {"Stavanger", "Las Capucas"}));
    }
};

QTEST_GUILESS_MAIN(tst_BeanBaseClient)
#include "tst_beanbaseclient.moc"
