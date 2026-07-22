#include <QtTest>

#include <QNetworkAccessManager>
#include <QNetworkRequest>

#include "core/settings.h"
#include "core/updatechecker.h"

// Tests for UpdateChecker::releaseInfoRequest() — the single GitHub releases
// request shared by the manual check and the hourly poll.
//
// The connection-cache attribute is the reason this suite exists. It has no
// visible runtime effect: drop it and the app still checks for updates
// correctly, and the only symptom is a Qt warning
//
//     QIODevice::read (QSslSocket): device not open
//
// ~30 s later in the user's log. That comes from Qt, not from us: GitHub closes
// the idle keep-alive connection, and Qt's HTTP/2 closure path
// (QHttp2ProtocolHandler::handleConnectionClosure -> QHttp2Connection::
// handleReadyRead) then reads from the already-closed socket without checking
// isOpen(). Setting the cache expiry to 0 makes us close it first, so that path
// never runs — see the comment on UpdateChecker::releaseInfoRequest() for the
// mechanism. Nothing BUT this suite would catch the attribute going missing
// again: no local build and no app run shows a symptom, only field logs, an hour
// at a time.

// Records every request the checker issues, without touching the network.
// createRequest() runs synchronously inside get(), so the recording is complete
// when the call returns; the request is redirected to a local path that cannot
// resolve, and no event loop is ever spun, so no reply handler runs.
class RecordingNam : public QNetworkAccessManager {
public:
    QList<QNetworkRequest> requests;

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override {
        requests.append(request);
        QNetworkRequest redirected(request);
        redirected.setUrl(QUrl(QStringLiteral("file:///nonexistent-decenza-test")));
        return QNetworkAccessManager::createRequest(op, redirected, outgoingData);
    }
};

class tst_UpdateChecker : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void releaseInfoRequestDropsConnectionImmediately();
    void releaseInfoRequestCarriesGithubHeaders();
    void theCheckPathIssuesTheSharedRequest();

private:
    // UpdateChecker's constructor dereferences Settings::app() to read
    // autoCheckUpdates, so Settings has to be real (the manager is only checked
    // for non-null there). Settings under DECENZA_TESTING persists to a
    // PID-scoped store (Settings::testQSettingsPath), so this touches no
    // developer config.
    QNetworkRequest buildRequest() {
        QNetworkAccessManager network;
        Settings settings;
        UpdateChecker checker(&network, &settings);
        return checker.releaseInfoRequest();
    }
};

void tst_UpdateChecker::releaseInfoRequestDropsConnectionImmediately()
{
    const QVariant expiry = buildRequest().attribute(
        QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute);

    QVERIFY2(expiry.isValid(),
             "ConnectionCacheExpiryTimeoutSecondsAttribute is unset, so Qt keeps the "
             "connection cached for the default 120 s and the idle-close warning returns");
    QCOMPARE(expiry.toInt(), 0);
}

void tst_UpdateChecker::releaseInfoRequestCarriesGithubHeaders()
{
    const QNetworkRequest request = buildRequest();

    QCOMPARE(request.header(QNetworkRequest::UserAgentHeader).toString(), QStringLiteral("Decenza"));
    QCOMPARE(request.rawHeader("Accept"), QByteArray("application/vnd.github.v3+json"));
    QVERIFY(request.url().isValid());
    QCOMPARE(request.url().host(), QStringLiteral("api.github.com"));
}

void tst_UpdateChecker::theCheckPathIssuesTheSharedRequest()
{
    // The regression worth guarding is a re-inlined request builder: both call
    // sites used to build the request by hand, and only one of them would have
    // acquired the cache policy. So this drives a real entry point and inspects
    // what actually went out — asserting that two calls to releaseInfoRequest()
    // match each other would only prove the helper is deterministic, and would
    // stay green through exactly the divergence it claims to catch.
    RecordingNam nam;
    Settings settings;
    UpdateChecker checker(&nam, &settings);

    checker.checkForUpdates();

    QCOMPARE(nam.requests.size(), 1);
    const QNetworkRequest issued = nam.requests.first();
    QCOMPARE(issued.attribute(QNetworkRequest::ConnectionCacheExpiryTimeoutSecondsAttribute).toInt(), 0);
    // Byte-for-byte the helper's output (QNetworkRequest::operator== compares
    // url, attributes and headers), so re-inlining this site fails here.
    QCOMPARE(issued, checker.releaseInfoRequest());

    // The hourly path, onPeriodicCheck(), is NOT driven here and is not covered:
    // it early-returns unless QGuiApplication::applicationState() is
    // ApplicationActive, which is never true for a windowless test binary — under
    // QTEST_MAIN as well as guiless, both tried. Re-inlining THAT site would slip
    // past this suite; it is one line calling the same helper, and the attribute
    // assertions above still pin what the helper produces.
}

QTEST_GUILESS_MAIN(tst_UpdateChecker)
#include "tst_updatechecker.moc"
