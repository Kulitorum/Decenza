#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "network/beanbaseclient.h"
#include "network/beanbase_blob.h"
#include "core/settings.h"
#include "mcp/mcptoolregistry.h"

// Registered standalone (mcptools_ai.cpp) precisely so this suite can drive
// the bean_search gather bridge without a MainController.
void registerBeanSearchTool(McpToolRegistry* registry, BeanBaseClient* client);

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
                    // Per-path routing; falls back to the single canned response.
                    QByteArray body = m_responseBody;
                    for (const auto& [pathPart, pathBody] : m_pathBodies) {
                        if (line.contains(pathPart)) { body = pathBody; break; }
                    }
                    const QByteArray resp =
                        "HTTP/1.1 " + m_statusLine + "\r\n"
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
    QList<QPair<QString, QString>> m_redirects;
    QStringList m_requestLines;
    bool m_hang = false;
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
        FakeBeanBaseServer server;
        server.respondWith("404 Not Found", "gone");
        BeanBaseClient client(&m_nam, &m_settings);
        QSignalSpy deadSpy(&client, &BeanBaseClient::bagLinkDead);
        QSignalSpy resolvedSpy(&client, &BeanBaseClient::bagLinkResolved);

        client.validateBagLink("canon-1", server.baseUrl() + "/products/gone");
        QVERIFY(deadSpy.wait(3000));
        QCOMPARE(deadSpy.count(), 1);
        QCOMPARE(deadSpy.first().at(0).toString(), QString("canon-1"));
        QCOMPARE(resolvedSpy.count(), 0);  // dead, never "resolved"

        // One GET per id per session: a second call is a no-op.
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
        server.respondForPath("/photo.jpg", "JPEGBYTES");

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
        QCOMPARE(f.readAll(), QByteArray("JPEGBYTES"));
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
        server.respondForPath("/photo.jpg", "DIRECTBYTES");

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
        server.respondForPath("/old.jpg", "OLDBYTES");
        server.respondForPath("/product-new",
            "<html><meta property=\"og:image\" content=\"" + base + "/new.jpg\"></html>");
        server.respondForPath("/new.jpg", "NEWBYTES");

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
        QCOMPARE(cachedBytes(), QByteArray("OLDBYTES"));

        QSignalSpy refreshed(&client, &BeanBaseClient::bagImageReady);
        client.refreshBagImage("bag-42", "Milk Blend", server.baseUrl() + "/product-new");
        QVERIFY(refreshed.wait(5000));
        QCOMPARE(refreshed.first().at(0).toString(), QString("bag-42"));
        QCOMPARE(cachedBytes(), QByteArray("NEWBYTES"));
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
        server.respondForPath("/old.jpg", "OLDBYTES");
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
        QCOMPARE(f.readAll(), QByteArray("OLDBYTES"));
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
            "BeanBaseClient: refusing unsafe bag image cache key \"../escape\"");
        client.ensureBagImage("../escape", "Nope", server.baseUrl() + "/product");
        QTest::ignoreMessage(QtWarningMsg,
            "BeanBaseClient: refusing unsafe bag image cache key \"a/b\"");
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
        client.search("ethiopia");
        QTest::qWait(600);                    // debounce fires; A sent and hung
        QCOMPARE(server.requestCount(), 1);   // A really is in flight

        client.search("colombia");            // distinct query supersedes A
        QTest::qWait(600);                    // debounce fires; doSend aborts A

        int ethiopiaTerminals = 0;
        QString status;
        for (const QList<QVariant>& sig : fail) {
            if (sig.at(0).toString() == QString("ethiopia")) {
                ++ethiopiaTerminals;
                status = sig.at(1).toString();
            }
        }
        QCOMPARE(ethiopiaTerminals, 1);       // abort stayed silent
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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refusing merge"));
        QCOMPARE(BeanBaseBlob::mergeBeanDetails(truncated, {{"origin", "Peru"}}), truncated);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refusing revert"));
        QCOMPARE(BeanBaseBlob::revertToCanonical(truncated), truncated);
        QVERIFY(!BeanBaseBlob::differsFromCanonical(truncated));

        const QString array = "[1,2,3]";
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refusing merge"));
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
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("fetchPageText got no readable text"));
        client.fetchPageText(server.baseUrl() + "/short");
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.last().at(1).toString(), QString("emptyPage"));

        // Non-text content (a PDF/image link) is a FORMAT failure, not a
        // confident "nothing found on the page".
        server.setContentType("application/pdf");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("fetchPageText got non-text content"));
        client.fetchPageText(pageUrl);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.last().at(1).toString(), QString("notAWebPage"));
        server.setContentType("text/html");

        // HTTP errors surface Qt's error string (reply->error() covers 4xx).
        server.respondWith("404 Not Found", "gone");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("fetchPageText failed for"));
        client.fetchPageText(server.baseUrl() + "/nothing-here");
        QVERIFY(failed.wait(5000));
        QVERIFY(!failed.last().at(1).toString().isEmpty());

        // http(s)-only gate: the URL is user-entered and the text is shipped
        // to a third-party AI — file:// must never be read. No server hit.
        const qsizetype requestsBefore = server.requestCount();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("fetchPageText rejected non-http url"));
        client.fetchPageText("file:///etc/hosts");
        QVERIFY(failed.wait(1000));
        QCOMPARE(failed.last().at(1).toString(), QString("invalidUrl"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("fetchPageText rejected non-http url"));
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
};

QTEST_GUILESS_MAIN(tst_BeanBaseClient)
#include "tst_beanbaseclient.moc"
