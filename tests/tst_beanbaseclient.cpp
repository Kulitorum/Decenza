#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QNetworkAccessManager>

#include "network/beanbaseclient.h"
#include "core/settings.h"
#include "core/settings_beanbase.h"

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
                    m_requestLines.append(QString::fromUtf8(
                        lineEnd > 0 ? req.left(lineEnd) : req));
                    const QByteArray resp =
                        "HTTP/1.1 " + m_statusLine + "\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: " + QByteArray::number(m_responseBody.size()) + "\r\n"
                        "Connection: close\r\n"
                        "\r\n" + m_responseBody;
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

    int requestCount() const { return m_requestLines.size(); }
    QStringList requestLines() const { return m_requestLines; }

private:
    QTcpServer m_server;
    QByteArray m_statusLine = "200 OK";
    // NOTE: no raw string literals anywhere in this file — moc miscounts the
    // braces inside "..." and silently drops every class declared after one,
    // which breaks the test class's vtable at link time.
    QByteArray m_responseBody = "{\"data\":[]}";
    QStringList m_requestLines;
};

class tst_BeanBaseClient : public QObject {
    Q_OBJECT

private:
    Settings m_settings;
    QNetworkAccessManager m_nam;
    QString m_origApiKey;

    // Mirrors the live wire shape (June 2026): {"meta":{…},"beans":[…]},
    // numeric ids, nullable fields. The "data" wrapper only appears on the
    // public endpoints — covered separately in parseBareArrayAndGarbage.
    static QByteArray sampleBeansJson() {
        return QByteArray(
            "{\"meta\":{\"tier\":\"Free Access\",\"remainingQuota\":1998},"
            "\"beans\":[{"
            "\"id\": 5188, \"roaster\": \"Prodigal Coffee\","
            "\"roast-name\": \"Buenos Aires Caturra - Colombia, washed\","
            "\"degree\": \"Medium\", \"type\": \"Espresso\","
            "\"link\": \"https://getprodigal.com/products/buenos-aires\","
            "\"origin\": \"Colombia\", \"region\": \"Huila\", \"producer\": \"Finca Buenos Aires\","
            "\"variety\": \"Caturra\", \"process\": \"Washed\","
            "\"min-elev\": 1700, \"max-elev\": \"1850\","
            "\"tasting-tag\": [\"Chocolate\", \"Caramel\"],"
            "\"general-tag\": \"Single Origin, Traceable\","
            "\"tasting\": \"chocolate, red apple\", \"soldout\": false, \"available\": true"
            "}]}");
    }

private slots:
    void init() {
        m_origApiKey = m_settings.beanbase()->beanBaseApiKey();
        m_settings.beanbase()->setBeanBaseApiKey("test_key_123");
    }

    void cleanup() {
        m_settings.beanbase()->setBeanBaseApiKey(m_origApiKey);
    }

    // ==========================================
    // testApiKey()
    // ==========================================

    void testApiKeyValidKey() {
        FakeBeanBaseServer server;
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::apiKeyTestResult);
        client.testApiKey();
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.first().at(0).toBool(), true);
        QCOMPARE(spy.first().at(1).toString(), QString("success"));
        QCOMPARE(server.requestCount(), 1);
        // The request carried the Bearer key path + limit=1.
        QVERIFY(server.requestLines().first().contains("/beans?limit=1"));
    }

    void testApiKeyInvalidKey() {
        FakeBeanBaseServer server;
        server.respondWith("401 Unauthorized", "{\"error\":\"Missing API Key\"}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::apiKeyTestResult);
        client.testApiKey();
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.first().at(0).toBool(), false);
        QCOMPARE(spy.first().at(1).toString(), QString("invalid"));
    }

    void testApiKeyNetworkError() {
        // Point at a port nothing listens on — connection refused.
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(QStringLiteral("http://127.0.0.1:1"));

        QSignalSpy spy(&client, &BeanBaseClient::apiKeyTestResult);
        client.testApiKey();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.first().at(0).toBool(), false);
        QCOMPARE(spy.first().at(1).toString(), QString("network"));
    }

    void testApiKeyMissingKey() {
        FakeBeanBaseServer server;
        m_settings.beanbase()->setBeanBaseApiKey("");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::apiKeyTestResult);
        client.testApiKey();
        // Emitted synchronously — no network round-trip, no request sent.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(1).toString(), QString("missing"));
        QCOMPARE(server.requestCount(), 0);
    }

    // ==========================================
    // search(): debounce, rate limit, cache
    // ==========================================

    void searchDebounceCoalescesKeystrokes() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK", sampleBeansJson());
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchResults);
        // Simulated fast typing — three queries inside the debounce window.
        client.searchBeanBase("p");
        client.searchBeanBase("pro");
        client.searchBeanBase("prodigal");

        QVERIFY(spy.wait(4000));
        // Only the final query was sent, exactly once.
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(server.requestLines().first().contains("search=prodigal"));
        QCOMPARE(spy.first().at(0).toString(), QString("prodigal"));
    }

    void searchEnforcesThreeSecondGap() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK", sampleBeansJson());
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchResults);
        client.searchBeanBase("first");
        QVERIFY(spy.wait(4000));          // request 1 sent after debounce
        QCOMPARE(server.requestCount(), 1);

        client.searchBeanBase("second");
        // Debounce (800 ms) elapses inside the 3 s window — the query must
        // be parked, not sent.
        QTest::qWait(1200);
        QCOMPARE(server.requestCount(), 1);

        // Once the 3 s window clears the parked query goes out.
        QVERIFY(spy.wait(4000));
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(server.requestLines().at(1).contains("search=second"));
    }

    void searchCacheHitSkipsNetwork() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK", sampleBeansJson());
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchResults);
        client.searchBeanBase("prodigal");
        QVERIFY(spy.wait(4000));
        QCOMPARE(server.requestCount(), 1);

        // Repeat (case-insensitive) — served from cache, synchronously.
        client.searchBeanBase("Prodigal");
        QCOMPARE(spy.count(), 2);
        QCOMPARE(server.requestCount(), 1);
        QCOMPARE(spy.at(1).at(1).toList().size(), 1);
    }

    void rateLimitAndQuota429sAreDistinguished() {
        // Both limits return 429 — only the body text differs. A quota-
        // exhausted user must see "done for today", not "try again shortly".
        FakeBeanBaseServer server;
        server.respondWith("429 Too Many Requests",
            "{\"error\":\"Rate limit exceeded. Maximum 1 request(s) per 3 second(s).\"}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::apiKeyTestResult);
        client.testApiKey();
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.first().at(1).toString(), QString("ratelimited"));

        server.respondWith("429 Too Many Requests",
            "{\"error\":\"Daily export quota exceeded.\"}");
        spy.clear();
        client.testApiKey();
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.first().at(1).toString(), QString("quota"));
    }

    void searchInvalidKeySignalsFailure() {
        FakeBeanBaseServer server;
        server.respondWith("401 Unauthorized", "{\"error\":\"bad key\"}");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchFailed);
        client.searchBeanBase("prodigal");
        QVERIFY(spy.wait(4000));
        QCOMPARE(spy.first().at(1).toString(), QString("invalid"));
    }

    // ==========================================
    // parseBeans() robustness
    // ==========================================

    void parseFullEntry() {
        const QVariantList entries = BeanBaseClient::parseBeans(sampleBeansJson());
        QCOMPARE(entries.size(), 1);
        const QVariantMap bean = entries.first().toMap();
        // Numeric id arrives as an opaque string.
        QCOMPARE(bean["id"].toString(), QString("5188"));
        QCOMPARE(bean["roasterName"].toString(), QString("Prodigal Coffee"));
        QCOMPARE(bean["degree"].toString(), QString("Medium"));
        // Elevations: numeric and string inputs both coerce.
        QCOMPARE(bean["minElevationM"].toInt(), 1700);
        QCOMPARE(bean["maxElevationM"].toInt(), 1850);
        // Tags: JSON-array and comma-joined-string forms both parse.
        QCOMPARE(bean["tastingTags"].toStringList(), QStringList({"Chocolate", "Caramel"}));
        QCOMPARE(bean["generalTags"].toStringList(), QStringList({"Single Origin", "Traceable"}));
        QCOMPARE(bean["soldout"].toBool(), false);
        QCOMPARE(bean["available"].toBool(), true);
    }

    void parseMissingFieldsUseDefaults() {
        const QVariantList entries = BeanBaseClient::parseBeans(
            "{\"data\":[{\"id\":\"7\",\"roaster\":\"Tiny Roaster\"}]}");
        QCOMPARE(entries.size(), 1);
        const QVariantMap bean = entries.first().toMap();
        QCOMPARE(bean["id"].toString(), QString("7"));
        QCOMPARE(bean["roastName"].toString(), QString());
        QCOMPARE(bean["tastingTags"].toStringList(), QStringList());
        QCOMPARE(bean["minElevationM"].toInt(), 0);
        // available defaults true (a listed bean is presumed orderable).
        QCOMPARE(bean["available"].toBool(), true);
    }

    void canonicalSearchFlow() {
        FakeBeanBaseServer server;
        server.respondWith("200 OK",
            "<div><li role=\"option\" data-autocomplete-value=\"abc-123\" "
            "data-roaster=\"Prodigal Coffee\" data-coffee-bag=\"Milk Blend\">x</li></div>");
        BeanBaseClient client(&m_nam, &m_settings);
        client.setVisualizerBaseUrl(server.baseUrl());

        QSignalSpy spy(&client, &BeanBaseClient::searchResults);
        // search() is the canonical path: debounced (350 ms), keyless, no
        // rate floor — and works with an empty Bean Base key.
        m_settings.beanbase()->setBeanBaseApiKey("");
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

    void parseBareArrayAndGarbage() {
        // Bare array (no {"data":…} wrapper) is tolerated.
        QCOMPARE(BeanBaseClient::parseBeans("[{\"id\":1}]").size(), 1);
        // Garbage and non-object entries produce empty results, not crashes.
        QCOMPARE(BeanBaseClient::parseBeans("not json").size(), 0);
        QCOMPARE(BeanBaseClient::parseBeans("{\"data\":\"oops\"}").size(), 0);
        QCOMPARE(BeanBaseClient::parseBeans("{\"data\":[42,\"x\"]}").size(), 0);
    }
};

QTEST_GUILESS_MAIN(tst_BeanBaseClient)
#include "tst_beanbaseclient.moc"
