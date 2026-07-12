// Tests for the remote MCP connector (McpRemoteAccess) added by the
// add-remote-mcp-connector change:
//   - capability token generation / rotation / constant-time comparison
//   - dedicated-listener route gating (non-MCP path → 404, wrong token → 404,
//     valid token → dispatched to McpServer)
//   - failed-token per-source rate limiting
//   - end-to-end initialize → notifications/initialized → tools/call through
//     the real loopback listener
//   - access-level enforcement is identical for remote sessions
//   - token rotation drops live connections and kills the old URL
//
// Drives a real McpRemoteAccess listener over a loopback TCP socket. The
// McpServer is linked with stub tool-registration functions (same pattern as
// tst_mcpserver_protocol.cpp) so no full tool graph is required.

#include <QtTest>
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
void registerProfileTools(McpToolRegistry*, ProfileManager*, Settings*) {}
void registerPresetsTools(McpToolRegistry*, Settings*, MainController*, MachineState*) {}
void registerRecipeTools(McpToolRegistry*, ShotHistoryStorage*, RecipeStorage*, MainController*, Settings*) {}
void registerSettingsReadTools(McpToolRegistry*, Settings*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*, AIManager*) {}
void registerDialingTools(McpToolRegistry*, MainController*, ProfileManager*, ShotHistoryStorage*, Settings*) {}
void registerControlTools(McpToolRegistry*, DE1Device*, MachineState*, ProfileManager*, MainController*, Settings*) {}
void registerWriteTools(McpToolRegistry*, ProfileManager*, ShotHistoryStorage*, Settings*, VisualizerUploader*, CoffeeBagStorage*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*, AIManager*) {}
void registerScaleTools(McpToolRegistry*, MachineState*) {}
void registerDeviceTools(McpToolRegistry*, BLEManager*, DE1Device*) {}
void registerDebugTools(McpToolRegistry*, MemoryMonitor*) {}
void registerMcpResources(McpResourceRegistry*, DE1Device*, MachineState*, ProfileManager*, ShotHistoryStorage*, MemoryMonitor*, Settings*) {}
void registerAgentTools(McpToolRegistry*) {}
void registerAITools(McpToolRegistry*, MainController*) {}

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
    void initTestCase()
    {
        // Best-effort isolation on platforms that honor test mode (Linux/CI).
        QStandardPaths::setTestModeEnabled(true);
        QSettings s("DecentEspresso", "DE1Qt");
        for (const char* key : kTouchedKeys)
            if (s.contains(key))
                m_savedSettings.insert(key, s.value(key));
    }

    void cleanupTestCase()
    {
        // Restore the real store exactly as we found it (remove keys we created,
        // restore prior values for keys that existed).
        QSettings s("DecentEspresso", "DE1Qt");
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
        for (int i = 0; i < 20; ++i)
            QVERIFY(!remote.failedTokenOverLimit("1.2.3.4"));
        // The 21st failure in the window trips the limit.
        QVERIFY(remote.failedTokenOverLimit("1.2.3.4"));
        // A different source has its own budget.
        QVERIFY(!remote.failedTokenOverLimit("5.6.7.8"));
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
