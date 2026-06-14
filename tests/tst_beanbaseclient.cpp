#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QRegularExpression>

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
                    const int lineEnd = req.indexOf("\r\n");
                    const QString line = QString::fromUtf8(
                        lineEnd > 0 ? req.left(lineEnd) : req);
                    m_requestLines.append(line);
                    if (m_hang)
                        return;  // hold the socket open, never respond: the
                                 // client's reply stays in-flight until aborted.
                    // Per-path routing; falls back to the single canned response.
                    QByteArray body = m_responseBody;
                    for (const auto& [pathPart, pathBody] : m_pathBodies) {
                        if (line.contains(pathPart)) { body = pathBody; break; }
                    }
                    const QByteArray resp =
                        "HTTP/1.1 " + m_statusLine + "\r\n"
                        "Content-Type: application/json\r\n"
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

    // Accept the request but never answer it — used to keep a reply in-flight so
    // the in-flight supersede/abort path is exercisable.
    void hangWithoutResponding() { m_hang = true; }

    int requestCount() const { return m_requestLines.size(); }
    QStringList requestLines() const { return m_requestLines; }

private:
    QTcpServer m_server;
    QByteArray m_statusLine = "200 OK";
    // NOTE: no raw string literals anywhere in this file — moc miscounts the
    // braces inside "..." and silently drops every class declared after one,
    // which breaks the test class's vtable at link time.
    QByteArray m_responseBody = "{\"data\":[]}";
    QList<QPair<QString, QByteArray>> m_pathBodies;
    QStringList m_requestLines;
    bool m_hang = false;
};

class tst_BeanBaseClient : public QObject {
    Q_OBJECT

private:
    Settings m_settings;
    QNetworkAccessManager m_nam;

private slots:
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
};

QTEST_GUILESS_MAIN(tst_BeanBaseClient)
#include "tst_beanbaseclient.moc"
