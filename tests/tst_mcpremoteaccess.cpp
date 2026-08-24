// Tests for the remote MCP connector (McpRemoteAccess) added by the
// add-remote-mcp-connector change:
//   - capability token generation / rotation / constant-time comparison
//   - dedicated-listener route gating (non-MCP path → 404, wrong token → 404,
//     valid token → dispatched to McpServer)
//   - failed-token per-source rate limiting, and the bound it puts on how much
//     debug log an unauthenticated caller can cause
//   - tunnel-proxied requests are logged as Funnel rather than as the loopback
//     address they arrive on, and are dropped without a reply
//   - end-to-end initialize → notifications/initialized → tools/call through
//     the real loopback listener
//   - access-level enforcement is identical for remote sessions
//   - token rotation drops live connections and kills the old URL
//
// Drives a real McpRemoteAccess listener over a loopback TCP socket. The
// McpServer is linked with stub tool-registration functions (same pattern as
// tst_mcpserver_protocol.cpp) so no full tool graph is required.

#include <QtTest>
#include "core/settings.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>

#include "mcp/mcpremoteaccess.h"
#include "mcp/mcpserver.h"
#include "mcp/mcptoolregistry.h"
#include "mcp/mcpresourceregistry.h"
#include "core/settings_mcp.h"

// ─── Stub register functions so McpServer links without the full tool graph ──
class DE1Device;
class MachineState;
class MainController;
class ProfileManager;
class ShotHistoryStorage;
class Settings;
class AccessibilityManager;
class ScreensaverVideoManager;
class BeanBaseClient;
class TranslationManager;
class BatteryManager;
class CoffeeBagStorage;
class BLEManager;
class MemoryMonitor;
class AIManager;
class VisualizerUploader;
class RecipeStorage;
void registerMachineTools(McpToolRegistry*, DE1Device*, MachineState*, MainController*, ProfileManager*) {}
void registerShotTools(McpToolRegistry*, ShotHistoryStorage*) {}
void registerProfileTools(McpToolRegistry*, ProfileManager*) {}
void registerPresetsTools(McpToolRegistry*, Settings*, MainController*) {}
void registerRecipeTools(McpToolRegistry*, ShotHistoryStorage*, RecipeStorage*, MainController*, Settings*) {}
void registerSettingsReadTools(McpToolRegistry*, Settings*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*, AIManager*) {}
void registerDialingTools(McpToolRegistry*, MainController*, ProfileManager*, ShotHistoryStorage*, Settings*) {}
void registerControlTools(McpToolRegistry*, DE1Device*, MachineState*, ProfileManager*, MainController*, Settings*) {}
void registerWriteTools(McpToolRegistry*, ProfileManager*, ShotHistoryStorage*, Settings*, VisualizerUploader*, CoffeeBagStorage*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*, AIManager*, BeanBaseClient*) {}
void registerScaleTools(McpToolRegistry*, MachineState*) {}
void registerDeviceTools(McpToolRegistry*, BLEManager*, DE1Device*) {}
void registerDebugTools(McpToolRegistry*, MemoryMonitor*) {}
void registerMcpResources(McpResourceRegistry*, DE1Device*, MachineState*, ProfileManager*, ShotHistoryStorage*, MemoryMonitor*, Settings*) {}
void registerAgentTools(McpToolRegistry*) {}
void registerAITools(McpToolRegistry*, MainController*) {}

// Counts the connector's unauthorized-request warnings while installed. A
// handler rather than QTest::ignoreMessage because the assertion is on HOW MANY
// lines a rejected caller can cause, which ignoreMessage cannot express.
static int s_unauthorizedWarnings = 0;
static int s_milestoneWarnings = 0;
static QString s_lastMilestoneWarning;
static void countUnauthorizedWarnings(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (type != QtWarningMsg || !msg.contains(QLatin1String("unauthorized request")))
        return;
    ++s_unauthorizedWarnings;
    // The line that carries the running count, so a submitted log can still tell
    // a stray probe from sustained hammering. Matched on "still being dropped",
    // which only the milestone says: "this minute" appears in the transition
    // line too ("...for the rest of this minute"), and matching that counted
    // both and made this assertion fail for a reason that was not the product's.
    if (msg.contains(QLatin1String("still being dropped"))) {
        ++s_milestoneWarnings;
        s_lastMilestoneWarning = msg;
    }
}

// Installs the counting handler and puts the previous one back on EVERY exit,
// including the early return QTest performs when an assertion inside the scope
// fails. A leaked handler would swallow warnings for the rest of the binary and
// break every later QTest::ignoreMessage with a cause unrelated to the failure.
class WarningCounterGuard {
public:
    WarningCounterGuard()
        : m_prior(qInstallMessageHandler(&countUnauthorizedWarnings))
    {
        s_unauthorizedWarnings = 0;
        s_milestoneWarnings = 0;
        s_lastMilestoneWarning.clear();
    }
    ~WarningCounterGuard() { qInstallMessageHandler(m_prior); }
    WarningCounterGuard(const WarningCounterGuard&) = delete;
    WarningCounterGuard& operator=(const WarningCounterGuard&) = delete;

private:
    QtMessageHandler m_prior;
};

class tst_McpRemoteAccess : public QObject {
    Q_OBJECT

    // Parsed HTTP response from the loopback listener.
    struct Resp {
        int status = 0;
        QByteArray rawBody;
        QJsonObject json;
        QString sessionId;
    };

    // Send one raw request to 127.0.0.1:port on a fresh connection and read the
    // full response (headers + Content-Length body). Optionally reuse a caller-
    // owned socket instead of opening a new one.
    // Pump the event loop until the client socket connects — waitForConnected
    // only services the client fd, not the listener's accept.
    static bool pumpConnected(QTcpSocket& sock, quint16 port)
    {
        sock.connectToHost(QHostAddress::LocalHost, port);
        QElapsedTimer ct;
        ct.start();
        while (sock.state() != QAbstractSocket::ConnectedState && ct.elapsed() < 3000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        return sock.state() == QAbstractSocket::ConnectedState;
    }

    // Read exactly one HTTP response (status + Content-Length body) from sock.
    static Resp readResponse(QTcpSocket& sock)
    {
        Resp r;
        QByteArray buf;
        int headerEnd = -1;
        qint64 contentLength = -1;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3000) {
            // processEvents services both the listener (accept + read + respond)
            // and this client socket (receive the response).
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            buf.append(sock.readAll());
            if (headerEnd < 0)
                headerEnd = static_cast<int>(buf.indexOf("\r\n\r\n"));
            if (headerEnd >= 0 && contentLength < 0) {
                contentLength = 0;
                for (const QByteArray& line : buf.left(headerEnd).split('\n')) {
                    const QByteArray lower = line.trimmed().toLower();
                    if (lower.startsWith("content-length:"))
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    else if (lower.startsWith("mcp-session-id:"))
                        r.sessionId = QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
                }
            }
            if (headerEnd >= 0 && contentLength >= 0
                && buf.size() >= headerEnd + 4 + contentLength)
                break;
            // Answered with nothing and hung up — the deliberate no-reply drop.
            // Without this the caller waits out the full timeout for a response
            // that is never coming, which is seconds per request in any test
            // that exercises the drop path. Guarded on an empty buffer so a
            // server that replies and then closes is still read to completion.
            if (buf.isEmpty() && sock.state() == QAbstractSocket::UnconnectedState)
                break;
        }

        const int firstLineEnd = static_cast<int>(buf.indexOf("\r\n"));
        if (firstLineEnd > 0) {
            const auto parts = buf.left(firstLineEnd).split(' ');
            if (parts.size() >= 2)
                r.status = parts[1].toInt();
        }
        if (headerEnd >= 0) {
            r.rawBody = buf.mid(headerEnd + 4);
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(r.rawBody, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                r.json = doc.object();
        }
        return r;
    }

    static Resp fetch(quint16 port, const QByteArray& request, QTcpSocket* reuse = nullptr)
    {
        QTcpSocket local;
        QTcpSocket* sock = reuse ? reuse : &local;
        if (sock->state() != QAbstractSocket::ConnectedState && !pumpConnected(*sock, port))
            return Resp{};
        sock->write(request);
        sock->flush();
        return readResponse(*sock);
    }

    // Send a request split into chunks, pumping the event loop between each so the
    // server exercises its partial-read reassembly path.
    static Resp fetchChunked(quint16 port, const QList<QByteArray>& chunks)
    {
        QTcpSocket sock;
        if (!pumpConnected(sock, port))
            return Resp{};
        for (const QByteArray& c : chunks) {
            sock.write(c);
            sock.flush();
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 60)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        }
        return readResponse(sock);
    }

    static QJsonObject initParams()
    {
        return QJsonObject{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1.0"}}}};
    }

    static QByteArray httpRequest(const QByteArray& method, const QByteArray& path,
                                  const QByteArray& body, const QByteArray& sessionId = {})
    {
        QByteArray req = method + " " + path + " HTTP/1.1\r\n";
        req += "Host: 127.0.0.1\r\n";
        req += "Content-Type: application/json\r\n";
        if (!sessionId.isEmpty())
            req += "Mcp-Session-Id: " + sessionId + "\r\n";
        req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        req += "\r\n";
        req += body;
        return req;
    }

    static QByteArray rpc(const QString& method, const QJsonObject& params = {}, int id = 1)
    {
        QJsonObject req{{"jsonrpc", "2.0"}, {"method", method}};
        if (id >= 0)
            req["id"] = id;
        if (!params.isEmpty())
            req["params"] = params;
        return QJsonDocument(req).toJson(QJsonDocument::Compact);
    }

    // Stand up McpServer + McpRemoteAccess bound to an ephemeral port. Registers
    // a read-level and a control-level tool for the integration/access tests.
    // Returns the listening port; token is read from `settings`.
    static quint16 startRemote(SettingsMcp& settings, McpServer& server, McpRemoteAccess& remote)
    {
        server.toolRegistry()->registerTool(
            "shots_get_detail", "read tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"ok", true}}; },
            "read");
        server.toolRegistry()->registerTool(
            "machine_start_espresso", "control tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"started", true}}; },
            "control");

        settings.setMcpEnabled(true);
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("https://example.ts.net"));
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.setRemoteMcpEnabled(true);

        remote.setMcpServer(&server);
        remote.setSettings(&settings);
        remote.refresh();
        return static_cast<quint16>(remote.listenPort());
    }

    // Complete the MCP handshake through the listener and return the session id.
    static QByteArray openSession(quint16 port, const QByteArray& token)
    {
        const QByteArray path = "/mcp/" + token;
        const QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1.0"}}}};
        Resp init = fetch(port, httpRequest("POST", path, rpc("initialize", params)));
        if (init.sessionId.isEmpty())
            return {};
        fetch(port, httpRequest("POST", path,
                                rpc("notifications/initialized", {}, -1),
                                init.sessionId.toUtf8()));
        return init.sessionId.toUtf8();
    }

private:
    // SettingsMcp uses a fixed-org QSettings that test mode does not redirect on
    // every platform, so these tests can touch the developer's real store. Snapshot
    // the keys we mutate and restore them verbatim afterwards — including
    // mcp/enabled, which the tests toggle and which the real app relies on.
    static constexpr const char* kTouchedKeys[] = {
        "mcp/enabled", "mcp/remoteEnabled", "mcp/remoteMode",
        "mcp/remotePort", "mcp/remoteCustomBaseUrl", "mcp/remoteToken"};
    QHash<QString, QVariant> m_savedSettings;

private slots:
    void init() { QTest::failOnWarning(); }
    void initTestCase()
    {
        // Best-effort isolation on platforms that honor test mode (Linux/CI).
        QStandardPaths::setTestModeEnabled(true);
        QSettings s(Settings::testQSettingsPath(), QSettings::IniFormat);
        for (const char* key : kTouchedKeys)
            if (s.contains(key))
                m_savedSettings.insert(key, s.value(key));
    }

    void cleanupTestCase()
    {
        // Restore the real store exactly as we found it (remove keys we created,
        // restore prior values for keys that existed).
        QSettings s(Settings::testQSettingsPath(), QSettings::IniFormat);
        for (const char* key : kTouchedKeys) {
            if (m_savedSettings.contains(key))
                s.setValue(key, m_savedSettings.value(key));
            else
                s.remove(key);
        }
    }

    // ── Token ────────────────────────────────────────────────────────────
    void tokenGeneration()
    {
        SettingsMcp settings;
        settings.rotateRemoteMcpToken();
        const QString token = settings.remoteMcpToken();
        QVERIFY(!token.isEmpty());
        // 128 bits base64url (no padding) → 22 chars, URL-safe alphabet only.
        QCOMPARE(token.size(), 22);
        QVERIFY(!token.contains('+'));
        QVERIFY(!token.contains('/'));
        QVERIFY(!token.contains('='));
        // Stable across reads (does not regenerate every call).
        QCOMPARE(settings.remoteMcpToken(), token);
    }

    void tokenRotation()
    {
        SettingsMcp settings;
        const QString before = settings.remoteMcpToken();
        settings.rotateRemoteMcpToken();
        const QString after = settings.remoteMcpToken();
        QVERIFY(!after.isEmpty());
        QVERIFY(before != after);
    }

    // ── Constant-time comparison (friend access) ──────────────────────────
    void constantTimeCompare()
    {
        SettingsMcp settings;
        settings.rotateRemoteMcpToken();
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        McpRemoteAccess remote;
        remote.setSettings(&settings);

        QVERIFY(remote.tokenMatches(token));
        QVERIFY(!remote.tokenMatches(QByteArray()));
        QVERIFY(!remote.tokenMatches("short"));
        // Same length, one byte off.
        QByteArray wrong = token;
        wrong[0] = wrong[0] == 'A' ? 'B' : 'A';
        QVERIFY(!remote.tokenMatches(wrong));
    }

    // ── Failed-token rate limiting (friend access) ────────────────────────
    void rateLimit()
    {
        McpRemoteAccess remote;
        // Under the per-minute budget: not limited.
        for (int i = 0; i < McpRemoteAccess::MaxFailedPerMinute; ++i)
            QVERIFY(!remote.failedTokenOverLimit("1.2.3.4"));
        // The next failure in the window trips the limit.
        QVERIFY(remote.failedTokenOverLimit("1.2.3.4"));
        // A different source has its own budget.
        QVERIFY(!remote.failedTokenOverLimit("5.6.7.8"));

        // The budget is what bounds how much log an unauthenticated caller can
        // write, so it is pinned at a value small enough to matter. Twenty (the
        // value this replaced) let one source emit twenty warnings a minute,
        // which is enough to evict every other subsystem from the debug buffer.
        QVERIFY(McpRemoteAccess::MaxFailedPerMinute <= 5);
    }

    // ── Tunnel-proxied requests are named as such, and get no reply ───────
    // Behind an embedded tunnel every remote client arrives as 127.0.0.1, so an
    // untagged log line invites the reader to dismiss a real scan as their own
    // on-device traffic. Driven through startListener() directly rather than
    // refresh(), so the assertion holds in builds compiled without tsnet.
    void tunnelProxiedRequestsAreTaggedAndUnanswered()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        settings.setMcpEnabled(true);
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.rotateRemoteMcpToken();
        remote.setMcpServer(&server);
        remote.setSettings(&settings);
        remote.startListener(/*bindLoopbackOnly=*/true);
        const quint16 port = static_cast<quint16>(remote.listenPort());
        QVERIFY(port != 0);

        // The loopback peer address is NOT what the line reports.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request from Funnel \\(public internet\\)"));
        Resp wrong = fetch(port, httpRequest("POST", "/mcp/not-the-real-token", rpc("initialize")));
        // No status line and no body: the connection was closed without a
        // reply, so nothing confirms a live service to whoever guessed.
        QCOMPARE(wrong.status, 0);
        QVERIFY(wrong.rawBody.isEmpty());

        // Same for malformed framing from a STRANGER, refused before any token
        // is parsed: Mode C answers these 404 (contentLengthValidation), a
        // tunnel says nothing.
        const QByteArray malformed =
            "POST /mcp/x HTTP/1.1\r\nHost: x\r\nContent-Length: notanumber\r\n\r\n";
        QCOMPARE(fetch(port, malformed).status, 0);

        // But the same framing failure from a client that DOES hold the token
        // still gets its 404. Silence there is indistinguishable from a dropped
        // network, so a legitimate client would retry a request that can never
        // succeed — and someone who already knows the token learns nothing from
        // the reply. The request line carries the token even though the framing
        // is bad, which is what makes the two cases separable.
        const QByteArray malformedFromHolder =
            "POST /mcp/" + settings.remoteMcpToken().toUtf8()
            + " HTTP/1.1\r\nHost: x\r\nContent-Length: notanumber\r\n\r\n";
        QCOMPARE(fetch(port, malformedFromHolder).status, 404);

        // A valid token still works through the same listener — the silence is
        // for failed authorization only, not for the route itself.
        Resp ok = fetch(port, httpRequest("POST", "/mcp/" + settings.remoteMcpToken().toUtf8(),
                                          rpc("initialize", initParams())));
        QCOMPARE(ok.status, 200);
    }

    // ── A rejected caller cannot keep writing to the log ──────────────────
    // Counts the warnings the REAL listener emits, not a re-derivation of its
    // decision tree: an unauthenticated caller who keeps knocking must stop
    // costing log lines, or it can evict every other subsystem's evidence from
    // the fixed-size debug buffer.
    void sustainedRejectionStopsCostingLogLines()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);

        // Drive the listener's own routing decision, not a re-derivation of it,
        // but without a TCP round trip per request.
        //
        // This used to open 105 fresh connections and read 105 replies, which
        // put wall clock inside the assertion. McpRateWindow's window is 60
        // seconds (mcpratewindow.h), so a loaded machine that stretched the
        // loop past a minute rolled the window mid-test: the count reset, the
        // suppression slot re-armed, three more per-request lines were written,
        // and `seen` never reached 100. The same slowness walked the loop into
        // pumpConnected's and readResponse's 3-second caps, and 105 of those
        // exceed QtTest's 300-second watchdog by construction — which is how
        // this test aborted the whole suite four times in one day, hung in
        // pumpConnected. Nothing about the log budget needs a socket to be
        // connected: routeRequest is the production decision, and the transport
        // that reaches it is already proven end-to-end by routeGating() and the
        // initialize round trip below.
        //
        // The socket is never handed to the listener, so it stays out of
        // m_sockets and nothing deletes it underneath the loop. Unconnected,
        // refuseRequest's write and close() are both no-ops — close() returns
        // early on UnconnectedState (qabstractsocket.cpp, QAbstractSocket::close)
        // — and the write's "device not open" warning is swallowed by the
        // counting handler, which forwards nothing it does not count.
        QTcpSocket offSocket;

        // Past the first milestone (100), so the branch that keeps recording
        // SCALE after per-request logging stops is actually exercised. Stopping
        // at a handful would leave that code able to be deleted with the suite
        // still green, and it is the whole reason the bound is not just silence.
        const int requests = 105;
        {
            WarningCounterGuard counting;
            for (int i = 0; i < requests; ++i)
                remote.routeRequest(&offSocket, QStringLiteral("POST"),
                                    QStringLiteral("/mcp/not-the-real-token"), {}, {});
        }

        // One line per request while under budget, one transition line, and one
        // milestone at 100 — never one per request.
        QCOMPARE(s_unauthorizedWarnings, McpRemoteAccess::MaxFailedPerMinute + 2);
        QVERIFY2(s_unauthorizedWarnings < requests,
                 "a rejected caller must not cost a log line per request");
        QCOMPARE(s_milestoneWarnings, 1);
        // The count itself is the payload — a milestone line that did not say
        // how many would bound the log without preserving the thing the bound
        // costs us.
        QVERIFY2(s_lastMilestoneWarning.contains(QLatin1String("100 unauthorized requests")),
                 qPrintable(s_lastMilestoneWarning));
    }

    // ── Route gating through the real listener ────────────────────────────
    void routeGating()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // Non-MCP path → bare 404 (unauthorized warning is expected).
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        Resp favicon = fetch(port, httpRequest("GET", "/favicon.ico", {}));
        QCOMPARE(favicon.status, 404);
        QVERIFY(favicon.rawBody.isEmpty());

        // Wrong token → bare 404.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        Resp wrong = fetch(port, httpRequest("POST", "/mcp/not-the-real-token",
                                             rpc("initialize")));
        QCOMPARE(wrong.status, 404);

        // Valid token, initialize → dispatched to McpServer (200 JSON-RPC).
        const QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1.0"}}}};
        Resp ok = fetch(port, httpRequest("POST", "/mcp/" + token, rpc("initialize", params)));
        QCOMPARE(ok.status, 200);
        QVERIFY(ok.json.contains("result"));
        QVERIFY(!ok.sessionId.isEmpty());
    }

    // ── End-to-end: initialize → tools/call through the listener ──────────
    void integrationInitializeListToolsCall()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();
        const QByteArray path = "/mcp/" + token;

        const QByteArray sid = openSession(port, token);
        QVERIFY(!sid.isEmpty());

        // tools/list surfaces the registered read tool.
        Resp list = fetch(port, httpRequest("POST", path, rpc("tools/list", {}, 2), sid));
        QCOMPARE(list.status, 200);
        const QJsonArray tools = list.json["result"].toObject()["tools"].toArray();
        QVERIFY(tools.size() >= 1);

        // tools/call on the read tool succeeds (default access level = Monitor).
        Resp call = fetch(port, httpRequest("POST", path,
            rpc("tools/call", QJsonObject{{"name", "shots_get_detail"},
                                          {"arguments", QJsonObject{}}}, 3), sid));
        QCOMPARE(call.status, 200);
        const QJsonObject result = call.json["result"].toObject();
        QVERIFY(!result.isEmpty());
        QVERIFY(!result.contains("error"));
    }

    // ── Remote sessions honor access level identically to LAN ─────────────
    void remoteHonorsAccessLevel()
    {
        SettingsMcp settings;
        McpServer server;   // no Settings wired → access level defaults to Monitor(0)
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();
        const QByteArray path = "/mcp/" + token;

        const QByteArray sid = openSession(port, token);
        QVERIFY(!sid.isEmpty());

        // A control-category tool is above Monitor level → rejected for the
        // remote session, exactly as it would be on the LAN path. The rejection
        // surfaces as a top-level JSON-RPC error.
        Resp call = fetch(port, httpRequest("POST", path,
            rpc("tools/call", QJsonObject{{"name", "machine_start_espresso"},
                                          {"arguments", QJsonObject{}}}, 4), sid));
        QCOMPARE(call.status, 200);
        QVERIFY2(call.json.contains("error"), call.rawBody.constData());
        QVERIFY(call.json["error"].toObject()["message"].toString().contains("Access level"));
    }

    // ── Rotation revokes the old URL and drops live connections ───────────
    void rotationClosesSockets()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray oldToken = settings.remoteMcpToken().toUtf8();

        // Open a live connection and complete a request on the old token.
        QTcpSocket live;
        Resp init = fetch(port, httpRequest("POST", "/mcp/" + oldToken, rpc("initialize")), &live);
        QCOMPARE(init.status, 200);
        QCOMPARE(live.state(), QAbstractSocket::ConnectedState);

        // Rotate: the live socket must be dropped and the URL must change.
        const QString urlBefore = remote.connectorUrl();
        remote.rotateToken();
        QTRY_COMPARE(live.state(), QAbstractSocket::UnconnectedState);
        QVERIFY(remote.connectorUrl() != urlBefore);

        // The old token no longer resolves (bare 404).
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        Resp stale = fetch(port, httpRequest("POST", "/mcp/" + oldToken, rpc("initialize")));
        QCOMPARE(stale.status, 404);

        // The new token works.
        const QByteArray newToken = settings.remoteMcpToken().toUtf8();
        QVERIFY(newToken != oldToken);
        const QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1.0"}}}};
        Resp fresh = fetch(port, httpRequest("POST", "/mcp/" + newToken, rpc("initialize", params)));
        QCOMPARE(fresh.status, 200);
    }

    // ── connectorUrl composition (Mode C) ─────────────────────────────────
    void connectorUrlComposition()
    {
        SettingsMcp settings;
        McpRemoteAccess remote;
        remote.setSettings(&settings);
        settings.setMcpEnabled(true);
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpEnabled(true);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // No base URL → no connector URL.
        settings.setRemoteMcpCustomBaseUrl(QString());
        QVERIFY(remote.connectorUrl().isEmpty());

        // Non-https base → rejected.
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("http://insecure.example"));
        QVERIFY(remote.connectorUrl().isEmpty());

        // Valid https base → composed, trailing slash trimmed.
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("https://decenza.example.ts.net/"));
        QCOMPARE(remote.connectorUrl(),
                 QStringLiteral("https://decenza.example.ts.net/mcp/") + QString::fromUtf8(token));
    }

    // ── connectorUrl also requires the master MCP toggle ──────────────────
    void connectorUrlRequiresMcpEnabled()
    {
        SettingsMcp settings;
        McpRemoteAccess remote;
        remote.setSettings(&settings);
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpEnabled(true);
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("https://decenza.example.ts.net"));

        settings.setMcpEnabled(false);
        QVERIFY(remote.connectorUrl().isEmpty());   // MCP off → no live URL
        settings.setMcpEnabled(true);
        QVERIFY(!remote.connectorUrl().isEmpty());
    }

    // ── Content-Length validation (body cap + malformed) ──────────────────
    void contentLengthValidation()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // Content-Length beyond the 1 MB body cap → bare 404, connection closed.
        const QByteArray oversized = "POST /mcp/" + token +
            " HTTP/1.1\r\nHost: x\r\nContent-Length: 5000000\r\n\r\n";
        QCOMPARE(fetch(port, oversized).status, 404);

        // Non-numeric Content-Length must be rejected, not silently parsed as 0
        // (which would desync keep-alive framing).
        const QByteArray malformed = "POST /mcp/" + token +
            " HTTP/1.1\r\nHost: x\r\nContent-Length: notanumber\r\n\r\n";
        QCOMPARE(fetch(port, malformed).status, 404);
    }

    // ── Path boundary: trailing segment rejected, query string stripped ───
    void pathBoundary()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // A trailing segment after the token is not the exact route → 404.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        QCOMPARE(fetch(port, httpRequest("POST", "/mcp/" + token + "/extra",
                                         rpc("initialize", initParams()))).status, 404);

        // A query string after the token is stripped → authorized.
        Resp ok = fetch(port, httpRequest("POST", "/mcp/" + token + "?src=claude",
                                          rpc("initialize", initParams())));
        QCOMPARE(ok.status, 200);
        QVERIFY(ok.json.contains("result"));
    }

    // ── Partial-read reassembly (request split across TCP segments) ───────
    void partialRequestReassembly()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        const QByteArray full = httpRequest("POST", "/mcp/" + token,
                                            rpc("initialize", initParams()));
        // Split mid-request so the server must buffer across reads before it can
        // find the header terminator / complete the body.
        const int mid = static_cast<int>(full.size() / 2);
        Resp r = fetchChunked(port, {full.left(mid), full.mid(mid)});
        QCOMPARE(r.status, 200);
        QVERIFY(r.json.contains("result"));
    }

    // ── Keep-alive pipelining: two requests in one write, both processed ──
    void pipelinedRequests()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        const QByteArray path = "/mcp/" + settings.remoteMcpToken().toUtf8();

        // Two initialize requests concatenated into one write on one socket: the
        // drain loop must process both, creating two sessions.
        const QByteArray two = httpRequest("POST", path, rpc("initialize", initParams(), 1))
                             + httpRequest("POST", path, rpc("initialize", initParams(), 2));
        fetch(port, two);
        QTRY_COMPARE(server.activeSessionCount(), 2);
    }

    // ── Master MCP toggle stops the remote listener ───────────────────────
    void disabledWhenMcpOff()
    {
        SettingsMcp settings;
        McpServer server;
        McpRemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote);
        QVERIFY(port != 0);
        QCOMPARE(remote.statusString(), QStringLiteral("active"));

        settings.setMcpEnabled(false);
        QCOMPARE(remote.listenPort(), 0);
        QCOMPARE(remote.statusString(), QStringLiteral("off"));
        QVERIFY(remote.connectorUrl().isEmpty());
    }
};

QTEST_MAIN(tst_McpRemoteAccess)
#include "tst_mcpremoteaccess.moc"
