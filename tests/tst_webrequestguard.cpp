#include <QtTest>
#include <QSysInfo>

#include "network/webrequestguard.h"

// Header blocks as the shot server sees them: request line, headers, no body.
static QByteArray block(std::initializer_list<const char*> lines)
{
    QByteArray out;
    for (const char* l : lines)
        out += QByteArray(l) + "\r\n";
    return out;
}

class TestWebRequestGuard : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void headerValue_isCaseInsensitiveAndTrimmed()
    {
        const QByteArray b = block({"POST /api/x HTTP/1.1", "host:  192.168.1.5:8888 ", "Sec-Fetch-Site: same-origin"});
        QCOMPARE(WebRequestGuard::headerValue(b, "Host"), QStringLiteral("192.168.1.5:8888"));
        QCOMPARE(WebRequestGuard::headerValue(b, "sec-fetch-site"), QStringLiteral("same-origin"));
        QVERIFY(WebRequestGuard::headerValue(b, "Origin").isEmpty());
        // The request line is not a header, so "POST" never reads as one.
        QVERIFY(WebRequestGuard::headerValue(b, "POST /api/x HTTP/1.1").isEmpty());
    }

    void hostIsOurs_data()
    {
        QTest::addColumn<QString>("host");
        QTest::addColumn<bool>("ours");
        QTest::newRow("IPv4 with port") << "192.168.1.5:8888" << true;
        QTest::newRow("IPv6 with port") << "[fe80::1%en0]:8888" << true;
        QTest::newRow("localhost") << "LocalHost:8888" << true;
        QTest::newRow("single label from the router") << "tablet:8888" << true;
        QTest::newRow("mDNS") << "tablet.local:8888" << true;
        QTest::newRow("router suffix") << "tablet.lan" << true;
        QTest::newRow("FRITZ!Box") << "tablet.fritz.box:8888" << true;
        QTest::newRow("Tailscale MagicDNS") << "tablet.tail1234.ts.net:8888" << true;
        QTest::newRow("no Host header, not a browser") << "" << true;
        QTest::newRow("public domain pointed at the tablet") << "attacker.example:8888" << false;
        QTest::newRow("public domain, no port") << "coffee.example.com" << false;
        // Android reports "localhost" as the machine name; a prefix rule would pass this.
        QTest::newRow("localhost under the attacker's domain") << "localhost.attacker.example:8888" << false;
        const QString machine = QSysInfo::machineHostName().toLower();
        if (!machine.isEmpty() && machine != QLatin1String("localhost")) {
            QTest::newRow("this machine's name, exactly") << machine + ":8888" << true;
            QTest::newRow("this machine's name under the attacker's domain") << machine + ".attacker.example:8888" << false;
        }
    }

    void hostIsOurs()
    {
        QFETCH(QString, host);
        QFETCH(bool, ours);
        QCOMPARE(WebRequestGuard::hostIsOurs(host), ours);
    }

    void crossSiteReason_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("headers");
        QTest::addColumn<bool>("refused");

        const char* host = "Host: 192.168.1.5:8888";
        // Writes.
        QTest::newRow("same-origin fetch from the shot page")
            << "POST" << "/api/shot/5/metadata"
            << block({host, "Origin: http://192.168.1.5:8888", "Sec-Fetch-Site: same-origin"}) << false;
        QTest::newRow("same host over https, scheme ignored")
            << "POST" << "/api/shot/5/metadata"
            << block({host, "Origin: https://192.168.1.5:8888"}) << false;
        QTest::newRow("host case does not matter")
            << "POST" << "/api/shot/5/metadata"
            << block({"Host: Tablet.local:8888", "Origin: http://tablet.LOCAL:8888"}) << false;
        QTest::newRow("another site's page posting")
            << "POST" << "/api/shot/5/metadata"
            << block({host, "Origin: http://evil.example", "Sec-Fetch-Site: cross-site"}) << true;
        QTest::newRow("another site, port differs only")
            << "POST" << "/api/shot/5/metadata"
            << block({host, "Origin: http://192.168.1.5:9999"}) << true;
        QTest::newRow("opaque origin")
            << "POST" << "/api/shot/5/upload"
            << block({host, "Origin: null"}) << true;
        QTest::newRow("curl, no browser headers")
            << "POST" << "/api/command" << block({host, "Content-Type: application/json"}) << false;
        QTest::newRow("browser without Origin but cross-site")
            << "POST" << "/api/shot/5/delete" << block({host, "Sec-Fetch-Site: cross-site"}) << true;
        QTest::newRow("DNS rebinding: Origin and Host both the attacker's name")
            << "POST" << "/api/shot/5/metadata"
            << block({"Host: attacker.example:8888", "Origin: http://attacker.example:8888", "Sec-Fetch-Site: same-origin"}) << true;
        // A form post is a navigation, but the navigation exemption is for GET only.
        QTest::newRow("cross-site form post with Origin stripped by the browser")
            << "POST" << "/api/shot/5/metadata"
            << block({host, "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: navigate", "Sec-Fetch-Dest: document"}) << true;

        // Reads and GETs.
        QTest::newRow("Home Assistant polling telemetry")
            << "GET" << "/api/telemetry" << block({host, "User-Agent: HomeAssistant/2026.9"}) << false;
        QTest::newRow("curl on the wake POST")
            << "POST" << "/api/power/wake" << block({host}) << false;
        // Only over https or localhost: on plain http a browser sends no Sec-Fetch headers.
        QTest::newRow("img tag on another site aimed at an API GET")
            << "GET" << "/api/state"
            << block({host, "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: no-cors", "Sec-Fetch-Dest: image"}) << true;
        QTest::newRow("iframe on another site aimed at an API GET")
            << "GET" << "/api/state"
            << block({host, "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: navigate", "Sec-Fetch-Dest: iframe"}) << true;
        QTest::newRow("img tag from another port on this host")
            << "GET" << "/api/state"
            << block({host, "Sec-Fetch-Site: same-site", "Sec-Fetch-Mode: no-cors", "Sec-Fetch-Dest: image"}) << true;
        QTest::newRow("rebinding read of the shot list")
            << "GET" << "/api/shots"
            << block({"Host: attacker.example:8888", "Sec-Fetch-Site: same-origin", "Sec-Fetch-Mode: cors"}) << true;
        QTest::newRow("another site reading shots with CORS")
            << "GET" << "/api/shots"
            << block({host, "Origin: http://evil.example", "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: cors"}) << true;
        QTest::newRow("link from another site opening the shot page")
            << "GET" << "/shot/5"
            << block({host, "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: navigate", "Sec-Fetch-Dest: document"}) << false;
        QTest::newRow("another site framing the history page")
            << "GET" << "/"
            << block({host, "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: navigate", "Sec-Fetch-Dest: iframe"}) << true;
        QTest::newRow("the page's own fetch of its JSON")
            << "GET" << "/api/shot/5/outcome" << block({host, "Sec-Fetch-Site: same-origin", "Sec-Fetch-Mode: cors"}) << false;
        QTest::newRow("typed into the address bar")
            << "GET" << "/api/state" << block({host, "Sec-Fetch-Site: none", "Sec-Fetch-Mode: navigate"}) << false;
        QTest::newRow("MCP client posting from a script")
            << "POST" << "/mcp" << block({host, "Content-Type: application/json"}) << false;
        QTest::newRow("page on another site posting to MCP")
            << "POST" << "/mcp" << block({host, "Sec-Fetch-Site: cross-site", "Sec-Fetch-Mode: cors"}) << true;
    }

    void crossSiteReason()
    {
        QFETCH(QString, method);
        QFETCH(QString, path);
        QFETCH(QByteArray, headers);
        QFETCH(bool, refused);
        QCOMPARE(!WebRequestGuard::crossSiteReason(method, path, headers).isEmpty(), refused);
    }
};

QTEST_GUILESS_MAIN(TestWebRequestGuard)
#include "tst_webrequestguard.moc"
