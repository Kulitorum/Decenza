// Tests for CommunitySettingsClient (src/network/communitysettingsclient.{h,cpp}) —
// Bean Search Phase 4, the community-shots network client. A fake QTcpServer stands
// in for the Visualizer GET /api/community/shots endpoint; the client is pointed at
// it via setBaseUrl(). Routing is by request line: the bean-filter leg carries
// "bean_brand", the canonical leg carries "canonical_coffee_bag_id".
//
// NOTE: no raw string literals anywhere in this file — moc miscounts the braces
// inside R"(...)" and silently drops classes declared after one.

#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

#include "network/communitysettingsclient.h"
#include "core/settings.h"
#include "core/settings_visualizer.h"

namespace {

QVariantMap shotRow(const QString& id, const QString& user, int enjoyment)
{
    QVariantMap m;
    m[QStringLiteral("id")] = id;
    m[QStringLiteral("user_id")] = user;
    m[QStringLiteral("profile_title")] = QStringLiteral("Prof");
    m[QStringLiteral("bean_weight")] = 18.0;
    m[QStringLiteral("drink_weight")] = 36.0;
    m[QStringLiteral("duration")] = 30.0;
    m[QStringLiteral("espresso_enjoyment")] = enjoyment;
    m[QStringLiteral("start_time")] = QVariant(static_cast<qlonglong>(2000000000));
    return m;
}

QByteArray shotsBody(const QVariantList& rows)
{
    QJsonObject o;
    o[QStringLiteral("data")] = QJsonArray::fromVariantList(rows);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray profileBody(double tempC, const QString& title)
{
    QJsonObject o;
    o[QStringLiteral("espresso_temperature")] = tempC;
    o[QStringLiteral("title")] = title;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Fake endpoint. Routes a response body (and optional status) by a substring of the
// request line; accumulates each request's full header block so header assertions work.
class FakeCommunityServer : public QObject {
    Q_OBJECT
public:
    FakeCommunityServer()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (m_server.hasPendingConnections()) {
                QTcpSocket* sock = m_server.nextPendingConnection();
                connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
                    m_buffers[sock] += sock->readAll();
                    if (!m_buffers[sock].contains("\r\n\r\n"))
                        return;  // wait for the full header block
                    const QByteArray req = m_buffers.take(sock);
                    m_requests.append(QString::fromUtf8(req));
                    const QString line = QString::fromUtf8(req.left(req.indexOf("\r\n")));
                    if (m_hang)
                        return;  // never answer: the reply stays in-flight until aborted

                    QByteArray body = m_responseBody;
                    QByteArray statusLine = m_statusLine;
                    for (const auto& [part, pathBody, pathStatus] : m_routes) {
                        if (line.contains(part)) {
                            body = pathBody;
                            if (!pathStatus.isEmpty())
                                statusLine = pathStatus;
                            break;
                        }
                    }
                    const QByteArray resp =
                        "HTTP/1.1 " + statusLine + "\r\n"
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

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

    void respondWith(const QByteArray& statusLine, const QByteArray& body)
    {
        m_statusLine = statusLine;
        m_responseBody = body;
    }
    void respondForPath(const QString& part, const QByteArray& body,
                        const QByteArray& statusLine = QByteArray())
    {
        m_routes.append({part, body, statusLine});
    }
    void hangWithoutResponding() { m_hang = true; }

    int requestCount() const { return int(m_requests.size()); }
    QStringList requests() const { return m_requests; }

private:
    QTcpServer m_server;
    QByteArray m_statusLine = "200 OK";
    QByteArray m_responseBody = "{\"data\":[]}";
    struct Route { QString part; QByteArray body; QByteArray status; };
    QList<Route> m_routes;
    QStringList m_requests;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    bool m_hang = false;
};

} // namespace

class TstCommunitySettingsClient : public QObject
{
    Q_OBJECT

    Settings m_settings;
    QNetworkAccessManager m_nam;

private slots:
    void init() { QTest::failOnWarning(); }

    void happyPath_bothLegsMergedAndSummarised();
    void singleLeg_whenNoCanonicalId();
    void sessionCache_secondSearchNoServerHit();
    void notFound_latchesUnsupported_thenResetsOnBaseUrl();
    void settingsBaseUrlChange_resetsLatch();
    void serverError_mapsToNetwork_data();
    void serverError_mapsToNetwork();
    void malformedJson_mapsToParse_notCached();
    void latestWins_firstSuperseded();
    void degrade_canonicalFailsBeanSucceeds_notCached();
    void authHeader_absentWhenDisconnected_presentWhenConnected();
    void parseCommunityShots_static();
    void communityPageUrl_encodesAndCarriesFloor();
    void fetchShotProfile_successParsesTempTitleAndAuth();
    void fetchShotProfile_notFoundMapsToNetwork();
    void fetchShotProfile_latestWinsSuperseded();
    void fetchShotProfile_cachedSecondFetchNoServerHit();
    void sameKeyInFlightCoalesces_newWaiterNotSuperseded();
    void credentialChange_clearsCacheAndRefetches();
};

void TstCommunitySettingsClient::happyPath_bothLegsMergedAndSummarised()
{
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("bean_brand"),
                          shotsBody({shotRow("s1", "u1", 85), shotRow("s2", "u2", 85)}));
    server.respondForPath(QStringLiteral("canonical_coffee_bag_id"),
                          shotsBody({shotRow("s2", "u2", 85), shotRow("s3", "u3", 85)}));
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QStringLiteral("bag-uuid"));
    QVERIFY(ready.wait());
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(0).toString(),
             CommunitySettingsClient::searchKey(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QStringLiteral("bag-uuid")));
    const QVariantMap summary = ready.at(0).at(1).toMap();
    QCOMPARE(summary.value(QStringLiteral("shotCount")).toInt(), 3); // s1,s2,s3 (s2 deduped)
    QCOMPARE(summary.value(QStringLiteral("userCount")).toInt(), 3);
    QVERIFY(summary.contains(QStringLiteral("profiles")));
}

void TstCommunitySettingsClient::singleLeg_whenNoCanonicalId()
{
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("bean_brand"), shotsBody({shotRow("s1", "u1", 85)}));
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(ready.wait());
    QCOMPARE(server.requestCount(), 1); // exactly one leg
    // Pins the deviation: the client fetches all bean shots (items=100) and never
    // sends min_enjoyment (the floor is applied client-side in summarize + deep link).
    const QString reqLine = server.requests().first();
    QVERIFY2(reqLine.contains(QStringLiteral("items=100")), qPrintable(reqLine));
    QVERIFY2(!reqLine.contains(QStringLiteral("min_enjoyment")), qPrintable(reqLine));
}

void TstCommunitySettingsClient::sessionCache_secondSearchNoServerHit()
{
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("bean_brand"), shotsBody({shotRow("s1", "u1", 85)}));
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(ready.wait());
    const int hits = server.requestCount();

    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString()); // identical → cache
    QVERIFY(ready.wait());
    QCOMPARE(ready.count(), 2);
    QCOMPARE(server.requestCount(), hits); // no new server hit
}

void TstCommunitySettingsClient::notFound_latchesUnsupported_thenResetsOnBaseUrl()
{
    FakeCommunityServer server;
    server.respondWith("404 Not Found", "{}");
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::searchFailed);
    QSignalSpy avail(&client, &CommunitySettingsClient::apiAvailableChanged);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QStringLiteral("bag-uuid"));
    QVERIFY(failed.wait());
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("unsupported"));
    QCOMPARE(avail.count(), 1);
    QCOMPARE(client.apiAvailable(), false);

    // While latched: no network, deferred unsupported.
    QTest::qWait(30);                      // let the aborted sibling leg settle before sampling
    const int hits = server.requestCount();
    client.search(QStringLiteral("Other"), QStringLiteral("Bean"), QString());
    QVERIFY(failed.wait());
    QCOMPARE(failed.last().at(1).toString(), QStringLiteral("unsupported"));
    QCOMPARE(server.requestCount(), hits); // no new request

    // setBaseUrl resets the latch and clears the cache.
    client.setBaseUrl(server.baseUrl());
    QCOMPARE(client.apiAvailable(), true);
}

void TstCommunitySettingsClient::settingsBaseUrlChange_resetsLatch()
{
    // Drive the base URL through the SETTING (not setBaseUrl) so the ctor's
    // communityApiBaseUrlChanged connection is what resets the latch.
    FakeCommunityServer server;
    server.respondWith("404 Not Found", "{}");
    m_settings.visualizer()->setCommunityApiBaseUrl(server.baseUrl().toString());
    CommunitySettingsClient client(&m_nam, &m_settings);

    QSignalSpy failed(&client, &CommunitySettingsClient::searchFailed);
    QSignalSpy avail(&client, &CommunitySettingsClient::apiAvailableChanged);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(failed.wait());
    QCOMPARE(client.apiAvailable(), false);

    // A base change points at a different server → latch resets (no setBaseUrl call).
    m_settings.visualizer()->setCommunityApiBaseUrl(QStringLiteral("http://127.0.0.1:1"));
    QCOMPARE(client.apiAvailable(), true);
    QVERIFY(avail.count() >= 2);   // false, then true

    m_settings.visualizer()->setCommunityApiBaseUrl(QStringLiteral("https://visualizer.coffee")); // restore default
}

void TstCommunitySettingsClient::serverError_mapsToNetwork_data()
{
    QTest::addColumn<QByteArray>("statusLine");
    QTest::newRow("429") << QByteArray("429 Too Many Requests");
    QTest::newRow("500") << QByteArray("500 Internal Server Error");
}

void TstCommunitySettingsClient::serverError_mapsToNetwork()
{
    QFETCH(QByteArray, statusLine);
    FakeCommunityServer server;
    server.respondWith(statusLine, "{}");
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::searchFailed);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(failed.wait());
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("network"));
    QCOMPARE(client.apiAvailable(), true); // only 404 latches
}

void TstCommunitySettingsClient::malformedJson_mapsToParse_notCached()
{
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("bean_brand"), "not json at all");
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::searchFailed);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(failed.wait());
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("parse"));

    // Not cached: a retry hits the server again.
    const int hits = server.requestCount();
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(failed.wait());
    QVERIFY(server.requestCount() > hits);
}

void TstCommunitySettingsClient::latestWins_firstSuperseded()
{
    FakeCommunityServer server;
    server.hangWithoutResponding(); // keep the first search's legs in flight
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::searchFailed);
    client.search(QStringLiteral("AAA"), QStringLiteral("One"), QString());
    const QString firstKey = CommunitySettingsClient::searchKey(QStringLiteral("AAA"), QStringLiteral("One"), QString());
    // Let the first request reach the (hanging) server before displacing it.
    QTRY_VERIFY(server.requestCount() >= 1);
    client.search(QStringLiteral("BBB"), QStringLiteral("Two"), QString());

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), firstKey);
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("superseded"));

    QTest::qWait(0);              // drain the loop: exactly one supersede, no late duplicate
    QCOMPARE(failed.count(), 1);
}

void TstCommunitySettingsClient::degrade_canonicalFailsBeanSucceeds_notCached()
{
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("bean_brand"), shotsBody({shotRow("s1", "u1", 85)}));
    server.respondForPath(QStringLiteral("canonical_coffee_bag_id"), "{}", "500 Internal Server Error");
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QStringLiteral("bag-uuid"));
    QVERIFY(ready.wait());
    QCOMPARE(ready.at(0).at(1).toMap().value(QStringLiteral("shotCount")).toInt(), 1); // bean rows alone

    // Degraded result not cached: a retry hits the server again.
    const int hits = server.requestCount();
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QStringLiteral("bag-uuid"));
    QVERIFY(ready.wait());
    QVERIFY(server.requestCount() > hits);
}

void TstCommunitySettingsClient::authHeader_absentWhenDisconnected_presentWhenConnected()
{
    // Disconnected: no credentials → no Authorization header.
    m_settings.visualizer()->setVisualizerUsername(QString());
    m_settings.visualizer()->setVisualizerPassword(QString());
    {
        FakeCommunityServer server;
        server.respondForPath(QStringLiteral("bean_brand"), shotsBody({}));
        CommunitySettingsClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());
        QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
        client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
        QVERIFY(ready.wait());
        QVERIFY(!server.requests().first().contains(QStringLiteral("authorization"), Qt::CaseInsensitive));
    }

    // Connected: both credentials → Basic header present.
    m_settings.visualizer()->setVisualizerUsername(QStringLiteral("me@example.com"));
    m_settings.visualizer()->setVisualizerPassword(QStringLiteral("secret"));
    {
        FakeCommunityServer server;
        server.respondForPath(QStringLiteral("bean_brand"), shotsBody({}));
        CommunitySettingsClient client(&m_nam, &m_settings);
        client.setBaseUrl(server.baseUrl());
        QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
        client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
        QVERIFY(ready.wait());
        QVERIFY(server.requests().first().contains(QStringLiteral("Authorization: Basic"), Qt::CaseSensitive));
    }
    m_settings.visualizer()->setVisualizerUsername(QString());
    m_settings.visualizer()->setVisualizerPassword(QString());
}

void TstCommunitySettingsClient::parseCommunityShots_static()
{
    bool ok = false;
    const QVariantList rows = CommunitySettingsClient::parseCommunityShots(
        shotsBody({shotRow("s1", "u1", 85)}), &ok);
    QVERIFY(ok);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("s1"));

    ok = true;
    const QVariantList bad = CommunitySettingsClient::parseCommunityShots("<<not json>>", &ok);
    QVERIFY(!ok);
    QVERIFY(bad.isEmpty());
}

void TstCommunitySettingsClient::communityPageUrl_encodesAndCarriesFloor()
{
    m_settings.visualizer()->setCommunityMinEnjoyment(80);
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(QUrl(QStringLiteral("http://localhost:3000")));
    const QUrl url = client.communityPageUrl(QStringLiteral("Ritual & Co"), QStringLiteral("Ethiopia"));
    const QString s = url.toString();
    QVERIFY(s.startsWith(QStringLiteral("http://localhost:3000/community")));
    QVERIFY(s.contains(QStringLiteral("min_enjoyment=80")));
    QVERIFY(s.contains(QStringLiteral("bean_type=Ethiopia")));
    m_settings.visualizer()->setCommunityMinEnjoyment(70);
}

void TstCommunitySettingsClient::fetchShotProfile_successParsesTempTitleAndAuth()
{
    // Disconnected → no Authorization header; temp/title/json parsed from the profile.
    m_settings.visualizer()->setVisualizerUsername(QString());
    m_settings.visualizer()->setVisualizerPassword(QString());
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("/profile"), profileBody(91.5, QStringLiteral("Test Profile")));
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::shotProfileReady);
    client.fetchShotProfile(QStringLiteral("s9"));
    QVERIFY(ready.wait());
    QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("s9"));
    const QVariantMap r = ready.at(0).at(1).toMap();
    QVERIFY(std::abs(r.value("temperatureC").toDouble() - 91.5) < 1e-6);
    QCOMPARE(r.value("title").toString(), QStringLiteral("Test Profile"));
    QVERIFY(r.value("profileJson").toString().contains(QStringLiteral("espresso_temperature")));
    QVERIFY(!server.requests().first().contains(QStringLiteral("authorization"), Qt::CaseInsensitive));

    // Connected → Basic header present on the profile fetch too.
    m_settings.visualizer()->setVisualizerUsername(QStringLiteral("me@example.com"));
    m_settings.visualizer()->setVisualizerPassword(QStringLiteral("secret"));
    FakeCommunityServer server2;
    server2.respondForPath(QStringLiteral("/profile"), profileBody(90.0, QStringLiteral("P")));
    CommunitySettingsClient client2(&m_nam, &m_settings);
    client2.setBaseUrl(server2.baseUrl());
    QSignalSpy ready2(&client2, &CommunitySettingsClient::shotProfileReady);
    client2.fetchShotProfile(QStringLiteral("s1"));
    QVERIFY(ready2.wait());
    QVERIFY(server2.requests().first().contains(QStringLiteral("Authorization: Basic"), Qt::CaseSensitive));
    m_settings.visualizer()->setVisualizerUsername(QString());
    m_settings.visualizer()->setVisualizerPassword(QString());
}

void TstCommunitySettingsClient::fetchShotProfile_notFoundMapsToNetwork()
{
    FakeCommunityServer server;
    server.respondWith("404 Not Found", "{}");
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::shotProfileFailed);
    client.fetchShotProfile(QStringLiteral("s9"));
    QVERIFY(failed.wait());
    QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("s9"));
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("network"));
    QCOMPARE(client.apiAvailable(), true); // the profile endpoint never touches the community latch
}

void TstCommunitySettingsClient::fetchShotProfile_latestWinsSuperseded()
{
    FakeCommunityServer server;
    server.hangWithoutResponding();
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::shotProfileFailed);
    client.fetchShotProfile(QStringLiteral("a"));
    QTRY_VERIFY(server.requestCount() >= 1);
    client.fetchShotProfile(QStringLiteral("b"));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("a"));
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("superseded"));
}

void TstCommunitySettingsClient::fetchShotProfile_cachedSecondFetchNoServerHit()
{
    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("/profile"), profileBody(92.0, QStringLiteral("P")));
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::shotProfileReady);
    client.fetchShotProfile(QStringLiteral("s1"));
    QVERIFY(ready.wait());
    const int hits = server.requestCount();
    client.fetchShotProfile(QStringLiteral("s1")); // cached
    QVERIFY(ready.wait());
    QCOMPARE(ready.count(), 2);
    QCOMPARE(server.requestCount(), hits);
}

void TstCommunitySettingsClient::sameKeyInFlightCoalesces_newWaiterNotSuperseded()
{
    FakeCommunityServer server;
    server.hangWithoutResponding(); // keep the first search's legs in flight
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy failed(&client, &CommunitySettingsClient::searchFailed);
    client.search(QStringLiteral("AAA"), QStringLiteral("One"), QString());
    QTRY_VERIFY(server.requestCount() >= 1);     // first leg reached the hanging server
    const int hitsAfterFirst = server.requestCount();

    // A second consumer requests the SAME key while the first is in flight → coalesce:
    // the new waiter must NOT be failed with "superseded" (the old bug), and no
    // replacement request is issued — it rides the live leg.
    client.search(QStringLiteral("AAA"), QStringLiteral("One"), QString());
    QTest::qWait(0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(server.requestCount(), hitsAfterFirst);

    // Latest-wins preserved: a DIFFERENT key still supersedes the in-flight one.
    const QString firstKey =
        CommunitySettingsClient::searchKey(QStringLiteral("AAA"), QStringLiteral("One"), QString());
    client.search(QStringLiteral("BBB"), QStringLiteral("Two"), QString());
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), firstKey);
    QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("superseded"));
}

void TstCommunitySettingsClient::credentialChange_clearsCacheAndRefetches()
{
    m_settings.visualizer()->setVisualizerUsername(QStringLiteral("a@example.com"));
    m_settings.visualizer()->setVisualizerPassword(QStringLiteral("pw1"));

    FakeCommunityServer server;
    server.respondForPath(QStringLiteral("bean_brand"), shotsBody({shotRow("s1", "u1", 85)}));
    CommunitySettingsClient client(&m_nam, &m_settings);
    client.setBaseUrl(server.baseUrl());

    QSignalSpy ready(&client, &CommunitySettingsClient::resultsReady);
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(ready.wait());
    const int hits = server.requestCount();

    // Identical search → served from cache, no server hit.
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(ready.wait());
    QCOMPARE(server.requestCount(), hits);

    // A credential change switches accounts: the cached (other-account) summary must be
    // dropped so the next identical search re-fetches instead of serving stale data.
    m_settings.visualizer()->setVisualizerPassword(QStringLiteral("pw2"));
    client.search(QStringLiteral("Ritual"), QStringLiteral("Ethiopia"), QString());
    QVERIFY(ready.wait());
    QVERIFY(server.requestCount() > hits);

    m_settings.visualizer()->setVisualizerUsername(QString());
    m_settings.visualizer()->setVisualizerPassword(QString());
}

QTEST_GUILESS_MAIN(TstCommunitySettingsClient)

#include "tst_communitysettingsclient.moc"
