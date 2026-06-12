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
                    // Per-path routing (two-stage canonical flow tests);
                    // falls back to the single canned response.
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
};

class tst_BeanBaseClient : public QObject {
    Q_OBJECT

private:
    Settings m_settings;
    QNetworkAccessManager m_nam;

private slots:
    // ====================================================
    // search(): the canonical (Visualizer) path — keyless,
    // debounced (350 ms), session-cached.
    // ====================================================

    void canonicalSearchFlow() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK",
            "<div><li role=\"option\" data-autocomplete-value=\"abc-123\" "
            "data-roaster=\"Prodigal Coffee\" data-coffee-bag=\"Milk Blend\">x</li></div>");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchResults);
        // search() is the canonical path: debounced (350 ms), keyless, no
        // rate floor.
        client.search("prodigal mi");
        client.search("prodigal milk");
        QVERIFY(spy.wait(3000));
        QCOMPARE(server.requestCount(), 1);  // debounce coalesced
        QVERIFY(server.requestLines().first().contains("/canonical/autocomplete_coffee_bags"));
        const QVariantList entries = spy.first().at(1).toList();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().toMap()["visualizerCanonicalId"].toString(), QString("abc-123"));

        // Cache hit: same query emits synchronously, no second request.
        client.search("Prodigal Milk");
        QCOMPARE(spy.count(), 2);
        QCOMPARE(server.requestCount(), 1);
    }

    void parseCanonicalAutocompleteFragment() {
        // Mirrors the live fragment shape (June 2026): <li> rows with
        // data-autocomplete-value (canonical UUID) + data-roaster +
        // data-coffee-bag; roaster fragments carry data-roaster-website.
        const QByteArray html =
            "<div class=\"py-1\">"
            "<li class=\"x\" role=\"option\" data-autocomplete-value=\"e54d274c-fb79\" "
            "data-roaster=\"Prodigal Coffee\" data-coffee-bag=\"Milk Blend &amp; More\">"
            "Milk Blend (Prodigal Coffee)</li>"
            "<li role=\"option\" data-autocomplete-value=\"cb10e43b-fe52\" "
            "data-roaster=\"Prodigal Coffee\" data-roaster-website=\"https://getprodigal.com/\">"
            "Prodigal Coffee</li></div>";
        const QVariantList entries = BeanBaseClient::parseCanonicalAutocomplete(html);
        QCOMPARE(entries.size(), 2);
        const QVariantMap bag = entries.first().toMap();
        QCOMPARE(bag["id"].toString(), QString("e54d274c-fb79"));
        QCOMPARE(bag["visualizerCanonicalId"].toString(), QString("e54d274c-fb79"));
        QCOMPARE(bag["source"].toString(), QString("visualizer"));
        QCOMPARE(bag["roasterName"].toString(), QString("Prodigal Coffee"));
        QCOMPARE(bag["roastName"].toString(), QString("Milk Blend & More"));  // entity-decoded
        const QVariantMap roaster = entries.at(1).toMap();
        QCOMPARE(roaster["roasterWebsite"].toString(), QString("https://getprodigal.com/"));
        QVERIFY(!roaster.contains("roastName"));
        // Garbage degrades to empty, never throws.
        QCOMPARE(BeanBaseClient::parseCanonicalAutocomplete("<html>nope</html>").size(), 0);
    }

    void parseCanonicalPayloadTwoStage() {
        // require_roaster=true responses nest a JSON payload div inside the
        // matching <li>; keys map onto our blob vocabulary.
        const QByteArray html =
            "<div><li role=\"option\" data-autocomplete-value=\"e54d274c-fb79\" "
            "data-roaster=\"Prodigal Coffee\" data-coffee-bag=\"Milk Blend\">Milk Blend"
            "<div data-coffee-bag-payload-value=\"{&quot;roast_level&quot;:&quot;Light To Medium-light&quot;,"
            "&quot;country&quot;:&quot;Brazil, Colombia&quot;,&quot;processing&quot;:&quot;Natural&quot;,"
            "&quot;region&quot;:null,&quot;farmer&quot;:null}\"></div></li></div>";
        const QVariantMap attrs = BeanBaseClient::parseCanonicalPayload(html, "e54d274c-fb79");
        QCOMPARE(attrs["degree"].toString(), QString("Light To Medium-light"));
        QCOMPARE(attrs["origin"].toString(), QString("Brazil, Colombia"));
        QCOMPARE(attrs["process"].toString(), QString("Natural"));
        QVERIFY(!attrs.contains("region"));  // nulls dropped
        // Wrong id / missing payload -> empty map.
        QVERIFY(BeanBaseClient::parseCanonicalPayload(html, "other-id").isEmpty());
        QVERIFY(BeanBaseClient::parseCanonicalPayload("<li></li>", "e54d274c-fb79").isEmpty());
    }

    // ====================================================
    // Two-stage enrichment, gather bridge, blob helpers
    // (review follow-up for #1322)
    // ====================================================

    void fetchCanonicalDetailsTwoStageFlow() {
        FakeBeanBaseServer server;
        server.respondForPath("autocomplete_roasters",
            "<div><li role=\"option\" data-autocomplete-value=\"roaster-uuid-1\" "
            "data-roaster=\"Prodigal Coffee\" data-roaster-website=\"https://getprodigal.com/\">"
            "Prodigal Coffee</li></div>");
        server.respondForPath("require_roaster=true",
            "<div><li role=\"option\" data-autocomplete-value=\"bag-uuid-1\" "
            "data-roaster=\"Prodigal Coffee\" data-coffee-bag=\"Milk Blend\">Milk Blend"
            "<div data-coffee-bag-payload-value=\"{&quot;roast_level&quot;:&quot;Light&quot;,"
            "&quot;country&quot;:&quot;Brazil&quot;,&quot;processing&quot;:&quot;Natural&quot;}\">"
            "</div></li></div>");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        QVariantMap entry;
        entry["id"] = "bag-uuid-1";
        entry["roasterName"] = "Prodigal Coffee";
        entry["roastName"] = "Milk Blend";

        QSignalSpy spy(&client, &BeanBaseClient::canonicalDetails);
        client.fetchCanonicalDetails(entry);
        QVERIFY(spy.wait(4000));
        QCOMPARE(spy.first().at(0).toString(), QString("bag-uuid-1"));
        const QVariantMap attrs = spy.first().at(1).toMap();
        QCOMPARE(attrs["degree"].toString(), QString("Light"));     // roast_level remapped
        QCOMPARE(attrs["origin"].toString(), QString("Brazil"));    // country remapped
        QCOMPARE(attrs["process"].toString(), QString("Natural"));  // processing remapped
        QCOMPARE(server.requestCount(), 2);  // roaster lookup + scoped payload fetch

        // Second enrichment for the same roaster: UUID cached, ONE request.
        QVariantMap entry2 = entry;
        entry2["id"] = "bag-uuid-1";  // same bag again (cache only covers roaster)
        client.fetchCanonicalDetails(entry2);
        QVERIFY(spy.wait(4000));
        QCOMPARE(server.requestCount(), 3);
    }

    void beanSearchToolRespondsViaGraceWhenEnrichmentStalls() {
        // The canned single-li response has NO payload div, so enrichment
        // never emits canonicalDetails — the tool must still respond, via
        // the 4 s enrichment grace window. This is the worst failure mode
        // (hang) pinned to "responds with identity-only results".
        FakeBeanBaseServer server;
        server.respondWith("200 OK",
            "<div><li role=\"option\" data-autocomplete-value=\"abc-123\" "
            "data-roaster=\"Prodigal Coffee\" data-coffee-bag=\"Milk Blend\">x</li></div>");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        McpToolRegistry registry;
        registerBeanSearchTool(&registry, &client);

        // The payload-less response means enrichment can't parse — the
        // resulting warning is exactly the stall path under test.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("enrichment payload missing/unparseable"));

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
        server.respondWith("200 OK", "<div></div>");
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
