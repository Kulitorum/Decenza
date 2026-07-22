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
// never runs. Nothing in a normal test run or a local build would catch the
// attribute going missing again — only field logs would, an hour at a time.
class tst_UpdateChecker : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void releaseInfoRequestDropsConnectionImmediately();
    void releaseInfoRequestCarriesGithubHeaders();
    void bothCheckPathsShareOneRequest();

private:
    // UpdateChecker's constructor dereferences Settings::app() and reads
    // autoCheckUpdates, so both collaborators have to be real. Settings under
    // DECENZA_TESTING persists to a PID-scoped store (Settings::testQSettingsPath),
    // so this touches no developer config.
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

void tst_UpdateChecker::bothCheckPathsShareOneRequest()
{
    // checkForUpdates() and onPeriodicCheck() both go through the helper, so a
    // header or policy added for one is never missing from the other. Two calls
    // must be indistinguishable.
    const QNetworkRequest first = buildRequest();
    const QNetworkRequest second = buildRequest();

    QCOMPARE(first, second);
}

QTEST_GUILESS_MAIN(tst_UpdateChecker)
#include "tst_updatechecker.moc"
