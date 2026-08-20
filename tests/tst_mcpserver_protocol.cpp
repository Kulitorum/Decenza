// Verifies the MCP 2025-11-25 spec-upgrade behavior added by `feat(mcp): adopt
// spec version 2025-11-25`: protocol version negotiation across the supported
// set, MCP-Protocol-Version request header validation, Origin allowlist
// (DNS-rebinding protection), and the new response shape (`title`, `icons`,
// `$schema`, `structuredContent`, `resource_link` blocks).
//
// Drives McpServer through a real TCP socket pair (same pattern as
// tst_mcpserver_session.cpp). HTTP-level testing rather than friend-class
// access — observable behavior is what the wire format actually emits.

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPair>

#include "mcp/mcpserver.h"
#include "version.h"
#include "mcp/mcpsession.h"
#include "mcp/mcptoolregistry.h"
#include "mcp/mcpresourceregistry.h"
#include "core/settings.h"
#include "core/settings_mcp.h"

// Stub register functions — tests pin behavior at the protocol layer; no full
// tool/resource graph required (matches tst_mcpserver_session.cpp).
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
void registerMachineTools(McpToolRegistry*, DE1Device*, MachineState*, MainController*, ProfileManager*) {}
void registerShotTools(McpToolRegistry*, ShotHistoryStorage*) {}
void registerProfileTools(McpToolRegistry*, ProfileManager*) {}
void registerPresetsTools(McpToolRegistry*, Settings*, MainController*) {}
class RecipeStorage;
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

class tst_McpServerProtocol : public QObject {
    Q_OBJECT

private:
    struct HttpResponse {
        int statusCode = 0;
        QJsonObject jsonBody;        // empty when body isn't JSON or is missing
        QJsonArray jsonArrayBody;    // populated instead when the response body is a JSON array
        QString rawBody;
        QString sessionId;
        QString protocolVersion;     // value of MCP-Protocol-Version response header
        QString allowOrigin;         // value of Access-Control-Allow-Origin
    };

    // Fire one HTTP request at the server. Extra request headers (Origin,
    // MCP-Protocol-Version, etc.) are appended raw — caller controls casing.
    // A connection the TEST owns, so it stays open after the request returns.
    //
    // sendHttp() keeps its QTcpServer and client socket as stack locals, so the
    // connection dies the moment it returns. That is fine for a request/response
    // exchange and useless for a DEFERRED one: an in-app confirmation is
    // precisely "the client is still waiting on a held response", and with the
    // socket already gone the server correctly abandons the confirmation before
    // a test can answer it.
    //
    // Nothing needed this until the confirmation gate gained a socket-disconnect
    // backstop. That the backstop broke five confirmation tests at once is the
    // harness admitting it had never modelled the connection lifetime those
    // tests depend on.
    struct HeldConnection {
        QTcpServer tcp;
        QTcpSocket client;
        QTcpSocket* serverSocket = nullptr;

        bool open()
        {
            if (!tcp.listen(QHostAddress::LocalHost)) return false;
            client.connectToHost(QHostAddress::LocalHost, tcp.serverPort());
            if (!tcp.waitForNewConnection(1000)) return false;
            serverSocket = tcp.nextPendingConnection();
            return serverSocket && client.waitForConnected(1000);
        }

        void send(McpServer& server, const QByteArray& body, const QString& sessionId = {})
        {
            QByteArray headers = "Content-Type: application/json\r\n";
            if (!sessionId.isEmpty())
                headers += "Mcp-Session-Id: " + sessionId.toUtf8() + "\r\n";
            server.handleHttpRequest(serverSocket, "POST", "/mcp", headers, body);
        }
    };

    // One parser for both callers — sendHttp and readHeld.
    static HttpResponse parseHttpResponse(const QByteArray& raw)
    {
        HttpResponse out;
        const qsizetype firstLineEnd = raw.indexOf("\r\n");
        if (firstLineEnd > 0) {
            const QByteArray statusLine = raw.left(firstLineEnd);
            const auto parts = statusLine.split(' ');
            if (parts.size() >= 2) out.statusCode = parts[1].toInt();
        }

        for (const QByteArray& line : raw.split('\n')) {
            const QByteArray lower = line.trimmed().toLower();
            const auto extract = [&]() {
                return QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
            };
            if (lower.startsWith("mcp-session-id:"))
                out.sessionId = extract();
            else if (lower.startsWith("mcp-protocol-version:"))
                out.protocolVersion = extract();
            else if (lower.startsWith("access-control-allow-origin:"))
                out.allowOrigin = extract();
        }

        const qsizetype bodyStart = raw.indexOf("\r\n\r\n");
        if (bodyStart >= 0) {
            const QByteArray bodyBytes = raw.mid(bodyStart + 4);
            out.rawBody = QString::fromUtf8(bodyBytes);
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(bodyBytes, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                out.jsonBody = doc.object();
            else if (err.error == QJsonParseError::NoError && doc.isArray())
                out.jsonArrayBody = doc.array();
        }

        return out;
    }

    // Read whatever a held connection has been sent so far. Parsing is the same
    // as sendHttp's, so it lives in parseHttpResponse() and neither copy is free
    // to drift.
    static HttpResponse readHeld(HeldConnection& conn)
    {
        conn.client.waitForReadyRead(1000);
        return parseHttpResponse(conn.client.readAll());
    }

    static HttpResponse sendHttp(McpServer& server,
                                 const QByteArray& method,
                                 const QByteArray& body,
                                 const QString& sessionId = QString(),
                                 const QList<QPair<QByteArray, QByteArray>>& extraHeaders = {})
    {
        HttpResponse out;

        QTcpServer tcp;
        tcp.listen(QHostAddress::LocalHost);
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, tcp.serverPort());
        if (!tcp.waitForNewConnection(1000)) return out;
        QTcpSocket* serverSocket = tcp.nextPendingConnection();
        if (!serverSocket) return out;
        if (!client.waitForConnected(1000)) return out;

        QByteArray headers = "Content-Type: application/json\r\n";
        if (!sessionId.isEmpty())
            headers += "Mcp-Session-Id: " + sessionId.toUtf8() + "\r\n";
        for (const auto& kv : extraHeaders)
            headers += kv.first + ": " + kv.second + "\r\n";

        server.handleHttpRequest(serverSocket, method, "/mcp", headers, body);

        client.waitForReadyRead(1000);
        out = parseHttpResponse(client.readAll());

        serverSocket->close();
        client.close();
        return out;
    }

    static QByteArray rpcBody(const QString& method,
                              const QJsonObject& params = {},
                              int id = 1)
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = id;
        req["method"] = method;
        req["params"] = params;
        return QJsonDocument(req).toJson(QJsonDocument::Compact);
    }

    // A MODERN-era request body. The discriminator is
    // `_meta["io.modelcontextprotocol/protocolVersion"]`, which the 2026-07-28
    // schema makes required on every request and which no legacy revision
    // defines — `clientCapabilities` is required too, so it is always sent.
    // `withId = false` builds a notification.
    static QByteArray modernBody(const QString& method,
                                 const QJsonObject& args = {},
                                 int id = 1,
                                 const QString& version = QStringLiteral("2026-07-28"),
                                 bool withId = true)
    {
        QJsonObject meta;
        meta["io.modelcontextprotocol/protocolVersion"] = version;
        meta["io.modelcontextprotocol/clientCapabilities"] = QJsonObject{};
        meta["io.modelcontextprotocol/clientInfo"] =
            QJsonObject{{"name", "tst-modern"}, {"version", "1"}};

        QJsonObject params = args;
        params["_meta"] = meta;

        QJsonObject req;
        req["jsonrpc"] = "2.0";
        if (withId)
            req["id"] = id;
        req["method"] = method;
        req["params"] = params;
        return QJsonDocument(req).toJson(QJsonDocument::Compact);
    }

    static QByteArray notifyBody(const QString& method)
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["method"] = method;
        return QJsonDocument(req).toJson(QJsonDocument::Compact);
    }

    // initialize + notifications/initialized so subsequent requests aren't
    // rejected by the uninitialized-session guard.
    static QString openSession(McpServer& server, const QString& version)
    {
        QJsonObject params{
            {"protocolVersion", version},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto init = sendHttp(server, "POST", rpcBody("initialize", params));
        if (init.sessionId.isEmpty()) return {};
        sendHttp(server, "POST", notifyBody("notifications/initialized"), init.sessionId);
        return init.sessionId;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ─── Protocol version negotiation ──────────────────────────────────────

    void initializeNegotiatesRequestedVersion_data()
    {
        QTest::addColumn<QString>("requested");
        QTest::addColumn<QString>("expected");
        QTest::newRow("current")   << "2025-11-25" << "2025-11-25";
        QTest::newRow("prior")     << "2025-06-18" << "2025-06-18";
        // Dropped revisions take the same path as any other unsupported one:
        // the server answers with its preferred version. Kept as rows rather
        // than deleted, so the drop stays asserted rather than merely absent.
        QTest::newRow("dropped26")  << "2025-03-26" << "2025-11-25";
        QTest::newRow("dropped24")  << "2024-11-05" << "2025-11-25";
        // Unsupported → server returns its preferred version (first in list).
        QTest::newRow("ancient")   << "2023-01-01" << "2025-11-25";
    }

    void initializeNegotiatesRequestedVersion()
    {
        QFETCH(QString, requested);
        QFETCH(QString, expected);

        // An unsupported request is answered with the preferred version AND
        // logged at WARN — that line is what explains a "my client stopped
        // working after the update" report, and init()'s failOnWarning() would
        // otherwise fail the test for the server doing the right thing.
        // `requested != expected` is exactly the unsupported case.
        if (requested != expected) {
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression("Client requested unsupported protocol "
                                                    + QRegularExpression::escape(requested)));
        }

        McpServer server;
        QJsonObject params{
            {"protocolVersion", requested},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params));

        QCOMPARE(resp.statusCode, 200);
        const QString got = resp.jsonBody["result"].toObject()["protocolVersion"].toString();
        QCOMPARE(got, expected);
    }

    // `serverInfo.version` identifies the tool SURFACE, not the build, and the two
    // are genuinely different facts: 2.0.2 shipped both a 97-tool server and a
    // 66-tool one, so a version that followed the app would have read identically
    // across the change that halved the list. Clients cache the catalogue they
    // fetched at initialize and refresh only on reconnect (some not even then), so
    // this string is what makes a stale session diagnosable at a glance.
    //
    // Asserting it against the constant rather than a literal is the point: the
    // literal is what it was for the server's whole life, and a hand-typed one here
    // would let the two drift apart while both looked right.
    void initializeReportsSurfaceVersionAndAppVersion()
    {
        McpServer server;
        QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params));

        const QJsonObject info = resp.jsonBody["result"].toObject()["serverInfo"].toObject();
        QCOMPARE(info["name"].toString(), QString("Decenza MCP Server"));
        QCOMPARE(info["version"].toString(), QString::fromLatin1(McpSurfaceVersion));
        QCOMPARE(info["appVersion"].toString(), QString(VERSION_STRING));
        QVERIFY2(info["version"].toString() != info["appVersion"].toString()
                     || QString::fromLatin1(McpSurfaceVersion) == QString(VERSION_STRING),
                 "surface version and app version are separate facts");
    }

    // The server-level `instructions` field (#1162) is emitted unconditionally,
    // and was never version-sensitive: `schema/2024-11-05/schema.ts` already
    // declares `instructions?: string;` on `InitializeResult`. The gate this
    // once had rested on a false premise, and the data table that replaced it
    // claimed to catch "a revision re-added below the floor" — which it could
    // not, since no row requested the only revision that argument was about.
    //
    // Collapsed to a single test asserting the field is present and carries the
    // shot-citation rule. Renamed, because the old name described gating this
    // no longer does.
    void initializeCarriesShotCitationInstructions()
    {
        McpServer server;
        QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params));

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();
        QVERIFY(result.contains("instructions"));
        const QString instr = result["instructions"].toString();
        QVERIFY2(!instr.isEmpty(),
                 "instructions must be a non-empty string when emitted");
        QVERIFY2(instr.contains(QStringLiteral("date and time")),
                 "instructions must carry the shot date/time citation rule (#1162)");
    }

    // ─── MCP-Protocol-Version request header validation ────────────────────

    void protocolVersionHeaderUnsupportedReturns400()
    {
        // 400 is licensed for exactly one case: "If the server receives a request
        // with an invalid or unsupported MCP-Protocol-Version, it MUST respond
        // with 400 Bad Request" (2025-06-18 basic/transports, Protocol Version
        // Header). 2026-07-28 is a real revision we do not yet negotiate, and is
        // the value real clients are already sending — see the sibling test for
        // what must NOT be 400'd.
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());

        // The refusal is logged at WARN: the assistant is told, the user is not,
        // so the log line is the only thing that explains the silence. Expected
        // here rather than tolerated — init()'s failOnWarning() would otherwise
        // fail this test for the server doing exactly what it should.
        QTest::ignoreMessage(QtWarningMsg,
                             "[MCP][Server] Unsupported protocol version — header 2026-07-28, "
                             "session 2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid,
                             {{"MCP-Protocol-Version", "2026-07-28"}});

        QCOMPARE(resp.statusCode, 400);
        QVERIFY(resp.rawBody.contains("Unsupported MCP-Protocol-Version"));
    }

    void protocolVersionHeaderSupportedButNotNegotiatedIsServed()
    {
        // This test replaces one that asserted the opposite, and the old
        // assertion was our bug rather than the spec's rule. Matching the
        // negotiated version is a CLIENT SHOULD — "the protocol version sent by
        // the client SHOULD be the one negotiated during initialization" — and a
        // client SHOULD is not a server gate. 2024-11-05 is in
        // supportedProtocolVersions(), so refusing it was refusing a version we
        // plainly serve.
        //
        // Found by the official conformance suite: its
        // server-accepts-multiple-post-streams scenario negotiates 2025-11-25 and
        // then sends concurrent POSTs carrying `MCP-Protocol-Version:
        // 2025-03-26`. All three were answered 400.
        McpServer server;
        // One inline tool so tools/list has something whose shape can be read.
        // A bare server registers nothing, and an empty array would let this pass
        // for either behaviour.
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());

        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid,
                             {{"MCP-Protocol-Version", "2025-06-18"}});

        QCOMPARE(resp.statusCode, 200);
        QVERIFY(resp.jsonBody.contains("result"));

        // Served under the HEADER's version, not the session's. With only two
        // negotiable revisions the ONLY field separating them is the 2025-11-25
        // `$schema` dialect, so its absence distinguishes "answered under the
        // header" from "answered 200 under the negotiated 2025-11-25 with the
        // header ignored".
        //
        // This keyed on `title` at 2024-11-05 when written. Both premises moved
        // underneath it: that revision is no longer supported (it would now take
        // the 400 branch, testing something else entirely), and `title` became
        // unconditional once the floor rose to the revision that introduced it.
        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), 1);
        QVERIFY2(!tools.first().toObject()["inputSchema"].toObject().contains("$schema"),
                 "answered under the header version, so no 2025-11-25 field");
    }

    void protocolVersionHeaderMatchAccepted()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid,
                             {{"MCP-Protocol-Version", "2025-11-25"}});

        QCOMPARE(resp.statusCode, 200);
        QVERIFY(resp.jsonBody.contains("result"));
    }

    // `2025-03-26` is accepted even though that revision is no longer
    // negotiable, and treated as if absent. Dropping it from
    // supportedProtocolVersions() without this turned the ecosystem's own
    // fallback into a 400. Caught by the official conformance suite, whose
    // server-accepts-multiple-post-streams scenario negotiates 2025-11-25 and
    // then sends concurrent POSTs carrying exactly this header.
    void compatSentinelHeaderIsAcceptedNotRejected()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());

        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid,
                             {{"MCP-Protocol-Version", "2025-03-26"}});

        QCOMPARE(resp.statusCode, 200);

        // Treated as absent, so the SESSION's version governs. This is the line
        // that separates the sentinel from a supported header: a supported one
        // is honoured AS a version (see the sibling test below), the sentinel
        // selects nothing. `$schema` is a 2025-11-25 field, so serving under
        // either 2025-03-26 or the floor would drop it and still return 200.
        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), 1);
        QVERIFY2(tools.first().toObject()["inputSchema"].toObject().contains("$schema"),
                 "the sentinel must not downgrade the session's negotiated version");
    }

    void supportedHeaderDoesNotReVersionTheSession()
    {
        // Honouring a supported header answers ONE request; it must not write
        // back to the session. Otherwise a single stray header from a confused
        // client silently downgrades every later request, including those that
        // send no header at all — a failure that would look like the server
        // spontaneously forgetting what it negotiated.
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());

        // One request under the older supported header. `$schema` is the only
        // field distinguishing the two negotiable revisions, so its absence is
        // what proves the header was honoured.
        auto older = sendHttp(server, "POST", rpcBody("tools/list", {}, 1), sid,
                              {{"MCP-Protocol-Version", "2025-06-18"}});
        QCOMPARE(older.statusCode, 200);
        QVERIFY2(!older.jsonBody["result"].toObject()["tools"].toArray()
                      .first().toObject()["inputSchema"].toObject().contains("$schema"),
                 "answered under the header version");

        // The next request carries no header at all, so it falls back to the
        // session — which must still be what initialize negotiated.
        auto after = sendHttp(server, "POST", rpcBody("tools/list", {}, 2), sid);
        QCOMPARE(after.statusCode, 200);
        QVERIFY2(after.jsonBody["result"].toObject()["tools"].toArray()
                     .first().toObject()["inputSchema"].toObject().contains("$schema"),
                 "the session must not have been re-versioned by the earlier header");
    }

    void protocolVersionHeaderAbsentAccepted()
    {
        // Clients pre-dating the header requirement may omit it. The session
        // then carries the lowest supported revision — see
        // McpSession::protocolVersion() for why that is no longer the
        // 2025-03-26 the spec names.
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid);
        QCOMPARE(resp.statusCode, 200);
    }

    void autoRecoveredSessionAdoptsClientProtocolVersion()
    {
        // Regression for the lockout where a stale Mcp-Session-Id forced the
        // server to auto-create a new session, which kept the default
        // (lowest supported) protocol version. The very next version-mismatch
        // check then 400'd a client whose previous session had negotiated
        // 2025-11-25 — defeating the auto-recovery the comment claims to
        // provide. Auto-recovery must adopt the header.
        McpServer server;

        // Two live sessions so the fallback "reuse the sole session" branch
        // doesn't fire — we want to drive the auto-create path specifically.
        const QString sid1 = openSession(server, "2025-11-25");
        QVERIFY(!sid1.isEmpty());
        const QString sid2 = openSession(server, "2025-11-25");
        QVERIFY(!sid2.isEmpty());

        // Client sends a stale session id and the version it had previously
        // negotiated. Pre-fix this path returned HTTP 400.
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99),
                             "00000000-0000-0000-0000-000000000000",
                             {{"MCP-Protocol-Version", "2025-11-25"}});

        QCOMPARE(resp.statusCode, 200);
        QVERIFY(resp.jsonBody.contains("result"));
        QCOMPARE(resp.protocolVersion, QString("2025-11-25"));
    }

    void protocolVersionHeaderEchoedInResponse()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid);
        QCOMPARE(resp.protocolVersion, QString("2025-11-25"));
    }

    // ─── Origin allowlist ──────────────────────────────────────────────────

    void emptyOriginAccepted()
    {
        McpServer server;
        QJsonObject params{
            {"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params));
        QCOMPARE(resp.statusCode, 200);
        // Without an Origin header, server falls back to wildcard CORS.
        QCOMPARE(resp.allowOrigin, QString("*"));
    }

    void loopbackOriginAccepted()
    {
        McpServer server;
        QJsonObject params{
            {"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params), {},
                             {{"Origin", "http://localhost:3000"}});
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.allowOrigin, QString("http://localhost:3000"));
    }

    void loopbackIpOriginAccepted()
    {
        McpServer server;
        QJsonObject params{
            {"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params), {},
                             {{"Origin", "http://127.0.0.1:5173"}});
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.allowOrigin, QString("http://127.0.0.1:5173"));
    }

    void foreignOriginRejectedWith403()
    {
        McpServer server;
        QJsonObject params{
            {"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}}};
        QTest::ignoreMessage(QtWarningMsg,
                             "[MCP][Server] Rejecting request from disallowed Origin: "
                             "http://evil.example");
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params), {},
                             {{"Origin", "http://evil.example"}});
        QCOMPARE(resp.statusCode, 403);
        // No JSON-RPC body — request is rejected before parsing.
        QVERIFY(!resp.jsonBody.contains("result"));
        QVERIFY(resp.rawBody.contains("Origin not allowed"));
    }

    void foreignOriginRejectedBeforeJsonRpcParsing()
    {
        // Even with a malformed body, foreign Origin should 403 — the check
        // runs before JSON parsing so DNS-rebinding attempts can't even
        // smuggle in a parse-error response that leaks server fingerprint.
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg,
                             "[MCP][Server] Rejecting request from disallowed Origin: "
                             "http://evil.example");
        auto resp = sendHttp(server, "POST", "{not valid json}", {},
                             {{"Origin", "http://evil.example"}});
        QCOMPARE(resp.statusCode, 403);
    }

    // ─── tools/list shape (title, icons, JSON Schema dialect) ──────────────

    void toolsListCarriesTitleAndSchemaButNoIcons()
    {
        McpServer server;
        // Register one inline tool so tools/list has something to inspect.
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 2), sid);

        QCOMPARE(resp.statusCode, 200);
        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), 1);
        const QJsonObject t = tools[0].toObject();

        QCOMPARE(t["name"].toString(), QString("shots_get_detail"));
        // 2025-06-18: human-readable title separate from programmatic name.
        QCOMPARE(t["title"].toString(), QString("Shots Get Detail"));

        // Tools carry NO icons, at any protocol version. They used to, as base64
        // data URIs, and it cost 216 KB of a ~248 KB tools/list — 87% of the
        // payload, with 41 of 97 tools shipping the same generic fallback. Real
        // clients truncate a list that size (ChatGPT exposed 87 of 97 tools), so
        // this is the byte budget, not a style preference. Resources keep theirs:
        // see resourcesListAt2025_11_25EmitsTitleAndIcons.
        QVERIFY2(!t.contains("icons"), "tools/list must not carry icons");
        QVERIFY2(!QString::fromUtf8(QJsonDocument(t).toJson(QJsonDocument::Compact))
                      .contains(QStringLiteral("data:")),
                 "no tool record may carry an inline data: URI");

        // 2025-11-25: JSON Schema 2020-12 dialect declared.
        QCOMPARE(t["inputSchema"].toObject()["$schema"].toString(),
                 QString("https://json-schema.org/draft/2020-12/schema"));
    }

    // Order is (tier, name) and nothing else — not hash order, which is what it
    // was. Under hash order two runs of the same build could present tools in
    // different orders, so which tools a truncating client dropped was
    // unreproducible; that is how get_flow_calibration went missing while
    // clear_flow_calibration survived.
    void toolsListSortsByTierThenName()
    {
        McpServer server;
        auto reg = [&](const QString& name, McpToolTier tier) {
            server.toolRegistry()->registerTool(
                name, "t", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
                [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read", tier);
        };
        reg("zzz_core", McpTierCore);
        reg("aaa_niche", McpTierNiche);
        reg("mmm_standard", McpTierStandard);
        reg("aaa_core", McpTierCore);

        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 2), sid);
        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();

        QStringList names;
        for (const QJsonValue& v : tools) names << v.toObject()["name"].toString();
        QCOMPARE(names, QStringList({"aaa_core", "zzz_core", "mmm_standard", "aaa_niche"}));
    }

    // A merged tool is "disabled" only when EVERY verb is out of reach. Marking it
    // by its strictest verb instead would stamp "[DISABLED — requires 'Full'…]" on
    // `bag` for a Monitor client, which then stops calling action=list at all — the
    // same "the tool does not exist for that user" failure this whole change is
    // about. The schema's injected `action` enum is checked here too: it is built
    // from the action vector, and nothing else asserts that it is.
    void mergedToolListsUsablyAtTheLevelOfItsWeakestVerb()
    {
        McpServer server;   // no Settings → access level 0 (Monitor)
        registerStubActionsOn(server.toolRegistry());
        server.toolRegistry()->registerTool(
            "settings_only_tool", "needs Full",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "settings");

        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 3), sid);

        QJsonObject merged, singleVerb;
        for (const QJsonValue& v : resp.jsonBody["result"].toObject()["tools"].toArray()) {
            const QJsonObject t = v.toObject();
            if (t["name"].toString() == "stub_family") merged = t;
            if (t["name"].toString() == "settings_only_tool") singleVerb = t;
        }
        QVERIFY(!merged.isEmpty());
        QVERIFY2(!merged["description"].toString().startsWith("[DISABLED"),
                 "a merged tool with a read verb must stay callable-looking at Monitor");
        QVERIFY(singleVerb["description"].toString().startsWith("[DISABLED"));

        const QJsonObject schema = merged["inputSchema"].toObject();
        QVERIFY(schema["required"].toArray().contains(QJsonValue(QStringLiteral("action"))));
        QCOMPARE(schema["properties"].toObject()["action"].toObject()["enum"].toArray(),
                 QJsonArray({"list", "erase"}));
    }

    // ─── tools/call response: structuredContent + resource_link extraction ─

    void toolsCallEmitsStructuredContentAndTextBlock()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "stub_tool",
            "Returns a fixed payload",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject {
                return QJsonObject{{"answer", 42}, {"label", "ok"}};
            },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "stub_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();

        // 2025-06-18: structuredContent with the raw payload.
        const QJsonObject structured = result["structuredContent"].toObject();
        QCOMPARE(structured["answer"].toInt(), 42);
        QCOMPARE(structured["label"].toString(), QString("ok"));

        // `content` is required on a tool result at every revision, so the text
        // block is always present — not a concession to old clients.
        const QJsonArray content = result["content"].toArray();
        QVERIFY2(!content.isEmpty(), "content[] must always be present");
        bool hasText = false;
        for (const QJsonValue& v : content)
            if (v.toObject()["type"].toString() == "text") { hasText = true; break; }
        QVERIFY(hasText);
    }

    void toolsCallEmitsResourceLinkBlocksFromSideChannel()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "stub_listy_tool",
            "Returns a payload with _resourceLinks side-channel",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject {
                QJsonObject link1{{"uri", "decenza://shots/42"}, {"title", "Shot #42"}};
                QJsonObject link2{{"uri", "decenza://profiles/foo"}, {"title", "Foo"}};
                return QJsonObject{
                    {"items", QJsonArray{42}},
                    {"_resourceLinks", QJsonArray{link1, link2}}};
            },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "stub_listy_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();
        const QJsonArray content = result["content"].toArray();

        // Two resource_link blocks + one text block, in that order.
        int linkCount = 0;
        int textCount = 0;
        for (const QJsonValue& v : content) {
            const QString type = v.toObject()["type"].toString();
            if (type == "resource_link") ++linkCount;
            else if (type == "text") ++textCount;
        }
        QCOMPARE(linkCount, 2);
        QCOMPARE(textCount, 1);

        // The first resource_link block carries the expected URI/title/mimeType.
        const QJsonObject firstLink = content[0].toObject();
        QCOMPARE(firstLink["uri"].toString(), QString("decenza://shots/42"));
        QCOMPARE(firstLink["title"].toString(), QString("Shot #42"));
        QCOMPARE(firstLink["mimeType"].toString(), QString("application/json"));
        // MCP 2025-06-18: `name` is REQUIRED on resource_link blocks. Strict
        // clients drop the entire content[] entry when it's missing. When the
        // emitter doesn't provide one, the wrapper derives it from the uri's
        // last path segment ("42" for decenza://shots/42).
        QVERIFY2(firstLink.contains("name"),
                 "resource_link blocks must always include `name` (MCP 2025-06-18)");
        QCOMPARE(firstLink["name"].toString(), QString("42"));
        // The second link similarly derives its name from the path segment.
        const QJsonObject secondLink = content[1].toObject();
        QCOMPARE(secondLink["name"].toString(), QString("foo"));

        // structuredContent must NOT carry the side-channel field — it's
        // consumed by the wrapper and stripped from the structured payload.
        const QJsonObject structured = result["structuredContent"].toObject();
        QVERIFY2(!structured.contains("_resourceLinks"),
                 "buildToolCallResponse must strip _resourceLinks from structuredContent");
        QVERIFY(structured.contains("items"));
    }

    // ─── tools/call failure marking: isError ──────────────────────────────
    //
    // Tools report failure by returning a top-level `error` key in their result
    // object. The wrap step buries that key one level down inside the envelope,
    // so sendJsonRpcResponse's top-level `contains("error")` test can never see a
    // WRAPPED payload — until buildToolCallResponse transferred the signal onto
    // the envelope, tool failures shipped as unmarked successes (the sole
    // exception being the confirmation denial, which marked its envelope by hand).

    // Run at every negotiated version, not just the newest.
    //
    // The trap this originally guarded is gone with the pre-2025-06-18
    // revisions: the marking reads the RAW payload rather than
    // structuredContent, and testing structuredContent instead would once have
    // silently dropped isError for clients whose revision has no such key. Both
    // surviving revisions emit it, so that particular refactor is no longer
    // dangerous. Said plainly rather than left as a stale warning.
    //
    // The table stays because what it asserts now is still worth asserting and
    // still breakable: isError must not become conditional on the negotiated
    // version. Two rows cost microseconds.
    void toolsCallMarksErrorPayloadAsFailed_data()
    {
        QTest::addColumn<QString>("protocolVersion");
        QTest::newRow("2025-06-18") << "2025-06-18";
        QTest::newRow("2025-11-25") << "2025-11-25";
    }

    void toolsCallMarksErrorPayloadAsFailed()
    {
        QFETCH(QString, protocolVersion);

        McpServer server;
        server.toolRegistry()->registerTool(
            "stub_failing_tool",
            "Returns an error payload",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject {
                return QJsonObject{{"error", "Database unavailable"}};
            },
            "read");

        const QString sid = openSession(server, protocolVersion);
        QJsonObject params;
        params["name"] = "stub_failing_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);

        QCOMPARE(resp.statusCode, 200);

        // A tool that ran and failed is a successful protocol exchange: the
        // response carries `result`, never a JSON-RPC `error` (which is
        // reserved for protocol faults and would not deliver content[] at all).
        QVERIFY2(!resp.jsonBody.contains("error"),
                 "a failing tool must not produce a JSON-RPC error");

        const QJsonObject result = resp.jsonBody["result"].toObject();
        QVERIFY2(result["isError"].toBool(),
                 "a tool result carrying `error` must be marked isError: true");

        // The failure marking must not cost the error TEXT — a model reads the
        // text block to learn what went wrong.
        QString text;
        const QJsonArray content = result["content"].toArray();
        for (const QJsonValue& v : content)
            if (v.toObject()["type"].toString() == "text")
                text = v.toObject()["text"].toString();
        QVERIFY2(text.contains("Database unavailable"),
                 "the tool's error text must remain readable in content[]");
    }

    void toolsCallOmitsIsErrorOnSuccess()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "stub_ok_tool",
            "Returns a payload with no error key",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject {
                return QJsonObject{{"answer", 42}};
            },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "stub_ok_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();
        // Sparse-emit: absence means success. `isError: false` on every
        // successful call adds a key to every response and carries no signal.
        QVERIFY2(!result.contains("isError"),
                 "a successful tool call must omit isError entirely, never emit false");
    }

    // The async completion is a SEPARATE send chain: handleToolsCall returns a
    // `_deferred` sentinel, the response is suppressed, and sendAsyncToolResponse
    // emits it later. It carries the majority of real failures — 162 of the ~283
    // error sites are the async `respond(QJsonObject{{"error", …}})` form,
    // including every recipe tool, all of mcptools_write.cpp and ai_advisor_invoke.
    // Without this, someone giving the async completion its own envelope (or
    // re-adding a hand-rolled marking there, the very thing deleted from the
    // denial path) would silently revert most tool failures to unmarked successes
    // while the synchronous tests above stayed green.
    //
    // callAsyncTool invokes the handler synchronously, so `respond` runs — and
    // writes the response through sendAsyncToolResponse — before handleToolsCall
    // returns. That does NOT cover queued delivery, so the socket-disconnected
    // drop stays uncovered; that is a different concern.
    void asyncToolCallMarksErrorPayloadAsFailed()
    {
        McpServer server;
        server.toolRegistry()->registerAsyncTool(
            "stub_async_failing_tool",
            "Responds with an error payload",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&, std::function<void(QJsonObject)> respond) {
                respond(QJsonObject{{"error", "Could not open shot database"}});
            },
            "read");

        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "stub_async_failing_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);

        QCOMPARE(resp.statusCode, 200);
        QVERIFY2(!resp.jsonBody.contains("error"),
                 "a failing async tool must not produce a JSON-RPC error");

        const QJsonObject result = resp.jsonBody["result"].toObject();
        QVERIFY2(result["isError"].toBool(),
                 "an async tool result carrying `error` must be marked isError: true");

        QString text;
        const QJsonArray content = result["content"].toArray();
        for (const QJsonValue& v : content)
            if (v.toObject()["type"].toString() == "text")
                text = v.toObject()["text"].toString();
        QVERIFY2(text.contains("Could not open shot database"),
                 "the async tool's error text must remain readable in content[]");
    }

    // The other half of the rule the wrap-site comment defends: a fault that
    // happens BEFORE any tool runs is a protocol fault and stays a JSON-RPC
    // error. Without this, someone reading that comment as an inconsistency and
    // "unifying" the two — routing the unknown-tool path through
    // buildToolCallResponse — would make a typo'd tool name report as a
    // successful exchange, which is the failure this whole change exists to stop.
    void unknownToolStaysJsonRpcError()
    {
        McpServer server;

        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "no_such_tool_was_ever_registered";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);

        QCOMPARE(resp.statusCode, 200);
        QVERIFY2(resp.jsonBody.contains("error"),
                 "an unknown tool is a protocol fault and must produce a JSON-RPC error");
        QVERIFY2(!resp.jsonBody.contains("result"),
                 "a JSON-RPC response carries error or result, never both");

        // And it is Invalid params, not Internal error: the tools spec's own
        // example returns -32602 for `Unknown tool: …`, and it describes a bad
        // request the caller can fix, not a server fault. Asserted here rather
        // than in a slot of its own — one response, one place that pins it, so
        // the two halves cannot drift into contradicting each other.
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32602);
    }

    // `ttlMs` / `cacheScope` are 2026-07-28 fields and must not leak to a client
    // that negotiated anything older — they are additive fields a strict earlier
    // client has no schema for.
    //
    // HALF a test, deliberately, and worth naming as such: it asserts ABSENCE,
    // so it passes identically whether the gate works or the feature was never
    // implemented. The positive case — that the fields ARE emitted, with the
    // list/read TTL split — cannot be written until 2026-07-28 is negotiable,
    // which is the last step of this change by design. The task list carries
    // that as an explicit follow-up rather than leaving this looking finished.
    void cacheHintsDoNotLeakToLegacyRevisions()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail", "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read");
        server.resourceRegistry()->registerResource(
            "decenza://shots/42", "Shot 42", "A test shot", "application/json",
            []() -> QJsonObject { return QJsonObject{{"id", 42}}; });

        for (const QString& version : {QStringLiteral("2025-06-18"),
                                       QStringLiteral("2025-11-25")}) {
            const QString sid = openSession(server, version);
            QVERIFY(!sid.isEmpty());

            for (const char* method : {"tools/list", "resources/list"}) {
                auto resp = sendHttp(server, "POST",
                                     rpcBody(QString::fromLatin1(method), {}, 2), sid);
                QCOMPARE(resp.statusCode, 200);
                const QJsonObject result = resp.jsonBody["result"].toObject();
                QVERIFY2(!result.contains("ttlMs"),
                         qPrintable(QStringLiteral("%1 at %2 must omit ttlMs")
                                        .arg(QString::fromLatin1(method), version)));
                QVERIFY2(!result.contains("cacheScope"),
                         qPrintable(QStringLiteral("%1 at %2 must omit cacheScope")
                                        .arg(QString::fromLatin1(method), version)));
            }

            QJsonObject readParams;
            readParams["uri"] = "decenza://shots/42";
            auto read = sendHttp(server, "POST", rpcBody("resources/read", readParams, 3), sid);
            QCOMPARE(read.statusCode, 200);
            QVERIFY(!read.jsonBody["result"].toObject().contains("ttlMs"));
        }
    }

    // `resources/list` must not emit in QHash order. Qt randomizes the hash seed
    // per process, so an unsorted listing is stable within a run and different
    // across restarts — which is exactly the shape that defeats a client cache
    // while looking fine in any single run. 2026-07-28 makes deterministic order
    // an explicit SHOULD.
    //
    // Asserting ascending URI order rather than comparing two runs, because two
    // registries in ONE process share the seed and would agree even unsorted —
    // a same-process comparison cannot fail. Removing the sort therefore reddens
    // this with probability 1 - 1/n!, which at eight resources is 1 - 1/40320.
    // Not a certainty, and said out loud rather than implied.
    void resourcesListIsOrderedByUriNotHashOrder()
    {
        McpServer server;
        // Registered in deliberately non-alphabetical order.
        const QStringList uris{
            "decenza://shots/recent", "decenza://machine/state", "decenza://profiles/active",
            "decenza://tools/steam", "decenza://beans/current", "decenza://water/vessel",
            "decenza://equipment/grinder", "decenza://ai/knowledge"};
        for (const QString& uri : uris) {
            server.resourceRegistry()->registerResource(
                uri, "N", "D", "application/json",
                []() -> QJsonObject { return QJsonObject{}; });
        }

        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("resources/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray resources = resp.jsonBody["result"].toObject()["resources"].toArray();
        QCOMPARE(resources.size(), uris.size());

        QStringList got;
        for (const QJsonValue& v : resources)
            got << v.toObject()["uri"].toString();

        QStringList expected = uris;
        expected.sort();
        QCOMPARE(got, expected);
    }

    // ─── Modern era (2026-07-28): detection, envelope, refusals ────────────

    // Every legacy shape must still take the legacy path. This is the assertion
    // the whole change rests on, and it is asymmetric on purpose: mis-routing a
    // legacy request to modern breaks a working client with no recovery, while
    // mis-routing modern to legacy produces the error a modern client's own
    // detection is specified to fall back from.
    void legacyShapesStillRouteToLegacy()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail", "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read");

        // initialize — creates a session and answers a negotiated version.
        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());
        QCOMPARE(server.activeSessionCount(), 1);

        // A post-handshake call, no _meta anywhere.
        auto call = sendHttp(server, "POST", rpcBody("tools/list", {}, 2), sid);
        QCOMPARE(call.statusCode, 200);
        QVERIFY(call.jsonBody.contains("result"));
        QVERIFY2(!call.jsonBody["result"].toObject().contains("resultType"),
                 "a legacy result must not carry modern framing");

        // A notification.
        QCOMPARE(sendHttp(server, "POST", notifyBody("notifications/initialized"), sid).statusCode,
                 202);

        // NOT asserted here: a bare GET for SSE. Era detection runs only in the
        // POST branch, so a GET cannot reach it and "routes to legacy" is not a
        // property that test would establish. Opening a live SSE stream from a
        // test also hangs the run — the server holds it open, by design.

        // `params` present but carrying no modern `_meta` must still read legacy —
        // this is the shape a careless discriminator gets wrong.
        auto withParams = sendHttp(server, "POST",
                                   rpcBody("tools/list", QJsonObject{{"cursor", "x"}}, 3), sid);
        QCOMPARE(withParams.statusCode, 200);
        QVERIFY(!withParams.jsonBody["result"].toObject().contains("resultType"));
    }

    // A modern request creates NO session, which is the whole point of the era.
    void modernRequestCreatesNoSession()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail", "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read");

        QCOMPARE(server.activeSessionCount(), 0);
        auto resp = sendHttp(server, "POST", modernBody("tools/list", {}, 1));
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(server.activeSessionCount(), 0);
        QVERIFY2(resp.sessionId.isEmpty(),
                 "a stateless request must not be answered with a session id");
    }

    // Required on every modern result, plus the server identity a stateless
    // caller has no handshake to learn from.
    void modernResultCarriesResultTypeAndServerInfo()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail", "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read");

        auto resp = sendHttp(server, "POST", modernBody("tools/list", {}, 7));
        QCOMPARE(resp.statusCode, 200);

        const QJsonObject result = resp.jsonBody["result"].toObject();
        QCOMPARE(result["resultType"].toString(), QString("complete"));

        const QJsonObject meta = result["_meta"].toObject();
        const QJsonObject info = meta["io.modelcontextprotocol/serverInfo"].toObject();
        QCOMPARE(info["name"].toString(), QString("Decenza MCP Server"));
        QCOMPARE(info["version"].toString(), QString::fromLatin1(McpSurfaceVersion));

        // The positive half of the cache-hint contract, which could not be
        // written until a modern version was servable: list results carry both
        // fields, and the scope is private because tools/list is access-filtered.
        QCOMPARE(result["cacheScope"].toString(), QString("private"));
        QVERIFY(result.contains("ttlMs"));
        QVERIFY2(result["ttlMs"].toInt() > 0, "a list result is fixed for the process lifetime");
    }

    // A read result is LIVE data — machine telemetry — so its TTL must be zero,
    // not merely shorter than a list's. A cached machine state is wrong, not stale.
    void modernReadResultIsNotCacheable()
    {
        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://shots/42", "Shot 42", "A test shot", "application/json",
            []() -> QJsonObject { return QJsonObject{{"id", 42}}; });

        auto resp = sendHttp(server, "POST",
                             modernBody("resources/read",
                                        QJsonObject{{"uri", "decenza://shots/42"}}, 8));
        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();
        QVERIFY2(result.contains("ttlMs"), "an absent ttlMs also reads 0");
        QCOMPARE(result["ttlMs"].toInt(), 0);
        QCOMPARE(result["cacheScope"].toString(), QString("private"));
    }

    // A version we cannot serve gets UnsupportedProtocolVersionError carrying the
    // list to retry from — the recovery path for a client that invoked a method
    // directly instead of calling server/discover first.
    void modernUnsupportedVersionReturnsSupportedList()
    {
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern tools/list"));
        auto resp = sendHttp(server, "POST",
                             modernBody("tools/list", {}, 9, QStringLiteral("2099-01-01")));

        const QJsonObject error = resp.jsonBody["error"].toObject();
        QCOMPARE(error["code"].toInt(), -32022);
        const QJsonObject data = error["data"].toObject();
        QCOMPARE(data["requested"].toString(), QString("2099-01-01"));

        const QJsonArray supported = data["supported"].toArray();
        QVERIFY2(!supported.isEmpty(), "the client needs a list to retry from");
        bool hasModern = false;
        for (const QJsonValue& v : supported)
            if (v.toString() == QStringLiteral("2026-07-28")) hasModern = true;
        QVERIFY2(hasModern, "the advertised list must contain what we actually serve");
    }

    // "For the HTTP transport, this value MUST match the MCP-Protocol-Version
    // header; otherwise the server MUST return a 400 Bad Request."
    void modernHeaderBodyVersionMismatchIsRejected()
    {
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern tools/list"));
        auto resp = sendHttp(server, "POST", modernBody("tools/list", {}, 10), QString(),
                             {{"MCP-Protocol-Version", "2025-11-25"}});
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32020);
        // The spec MUST this test's own comment quotes. It was quoted and then
        // not asserted: the entire modern status mapping had no coverage, so
        // replacing modernHttpStatusForError's body with `return 200;` — exactly
        // the defect conformance found — left the suite green.
        QCOMPARE(resp.statusCode, 400);
    }

    // Removed by 2026-07-28, or replaced by subscriptions/listen — but all still
    // correct for legacy, which is why they are refused by era rather than by
    // deleting the handler.
    void modernEraRefusesRemovedMethods_data()
    {
        QTest::addColumn<QString>("method");
        QTest::newRow("ping")        << "ping";
        QTest::newRow("setLevel")    << "logging/setLevel";
        QTest::newRow("subscribe")   << "resources/subscribe";
        QTest::newRow("unsubscribe") << "resources/unsubscribe";
        QTest::newRow("initialize")  << "initialize";
    }

    void modernEraRefusesRemovedMethods()
    {
        QFETCH(QString, method);
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern "));
        auto resp = sendHttp(server, "POST", modernBody(method, {}, 11));
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32601);
        QCOMPARE(resp.statusCode, 404);
    }

    // `ping` must keep working for a LEGACY caller — the refusal above is scoped
    // to the era, not a deletion. Without this the previous test would pass just
    // as well if someone removed the handler outright.
    void legacyEraStillServesPing()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("ping", {}, 12), sid);
        QCOMPARE(resp.statusCode, 200);
        QVERIFY(resp.jsonBody.contains("result"));
    }

    // Control tools ARE reachable in the modern era now that a
    // session-independent limiter exists to charge them against — and they are
    // charged, which is the half that matters. A read verb must not spend the
    // control budget.
    void modernEraServesControlToolsAndChargesThem()
    {
        McpServer server;
        // Control tools need an access level that permits them; without Settings
        // the level is 0 (Monitor) and the call is denied before the limiter is
        // ever consulted, which would make this test green for the wrong reason.
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(0);
        server.setSettings(&settings);

        auto ran = std::make_shared<int>(0);
        server.toolRegistry()->registerTool(
            "machine_wake", "A control tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [ran](const QJsonObject&) -> QJsonObject { ++*ran; return QJsonObject{}; },
            "control");

        QJsonObject params;
        params["name"] = "machine_wake";
        params["arguments"] = QJsonObject{};

        auto resp = sendHttp(server, "POST", modernBody("tools/call", params, 13));
        QCOMPARE(resp.statusCode, 200);
        QVERIFY2(!resp.jsonBody.contains("error"), "a control tool is no longer refused");
        QCOMPARE(*ran, 1);
        // Served as MODERN, not merely served. Legacy auto-creates a session and
        // runs the same tool with the same observable result, so without this the
        // test passes with era detection stubbed to `return false`.
        QCOMPARE(resp.jsonBody["result"].toObject()["resultType"].toString(),
                 QString("complete"));
    }

    // Over the budget, the caller is refused — and the tool does not run. A
    // limiter that refuses after dispatch is not a limiter.
    void modernControlCallsAreRateLimitedPerCaller()
    {
        McpServer server;
        // Control tools need an access level that permits them; without Settings
        // the level is 0 (Monitor) and the call is denied before the limiter is
        // ever consulted, which would make this test green for the wrong reason.
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(0);
        server.setSettings(&settings);

        auto ran = std::make_shared<int>(0);
        server.toolRegistry()->registerTool(
            "machine_wake", "A control tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [ran](const QJsonObject&) -> QJsonObject { ++*ran; return QJsonObject{}; },
            "control");

        QJsonObject params;
        params["name"] = "machine_wake";
        params["arguments"] = QJsonObject{};

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Rate limit exceeded .* refusing control calls"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern tools/call"));

        bool sawRefusal = false;
        for (int i = 0; i < McpServer::RateLimitPerMinute + 5; ++i) {
            auto resp = sendHttp(server, "POST", modernBody("tools/call", params, 100 + i));
            if (resp.jsonBody.contains("error")) {
                QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32000);
                sawRefusal = true;
                break;
            }
        }
        QVERIFY2(sawRefusal, "an unbounded control surface is the thing this exists to prevent");
        QVERIFY2(*ran <= McpServer::RateLimitPerMinute,
                 "a refused call must not have run");
    }

    // A read verb must not spend the control budget. Without this the limiter
    // would look correct while throttling harmless calls.
    void modernReadToolsDoNotSpendTheControlBudget()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail", "A read tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read");

        QJsonObject params;
        params["name"] = "shots_get_detail";
        params["arguments"] = QJsonObject{};

        for (int i = 0; i < McpServer::RateLimitPerMinute + 5; ++i) {
            auto resp = sendHttp(server, "POST", modernBody("tools/call", params, 200 + i));
            QVERIFY2(!resp.jsonBody.contains("error"),
                     "a read verb must not be charged against the control budget");
            // As above: legacy does not rate-limit reads either, so the era has
            // to be asserted or this passes under legacy routing.
            QCOMPARE(resp.jsonBody["result"].toObject()["resultType"].toString(),
                     QString("complete"));
        }
    }

    // A modern notification gets 202 with no body, same as legacy.
    void modernNotificationAnswersWith202()
    {
        McpServer server;
        auto resp = sendHttp(server, "POST",
                             modernBody("notifications/initialized", {}, 0,
                                        QStringLiteral("2026-07-28"), /*withId=*/false));
        QCOMPARE(resp.statusCode, 202);
        QVERIFY(resp.rawBody.isEmpty());
    }

    // `initialize` must never offer a modern revision: that era has no handshake,
    // so a client reaching that code cannot speak one, and the fallback for an
    // unsupported request must stay a legacy revision.
    void initializeNeverNegotiatesAModernRevision()
    {
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Client requested unsupported protocol"));
        QJsonObject params{
            {"protocolVersion", "2026-07-28"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params));

        const QString got = resp.jsonBody["result"].toObject()["protocolVersion"].toString();
        QCOMPARE(got, QString("2025-11-25"));
        QVERIFY2(!McpServer::isModernProtocolVersion(got),
                 "initialize must answer with a handshake-based revision");
    }

    // A server MUST implement server/discover. Both fields it requires, plus the
    // cache hints DiscoverResult inherits from CacheableResult — which the
    // change's own proposal missed until the schema was read.
    void serverDiscoverAdvertisesVersionsAndCapabilities()
    {
        McpServer server;
        auto resp = sendHttp(server, "POST", modernBody("server/discover", {}, 20));
        QCOMPARE(resp.statusCode, 200);

        const QJsonObject result = resp.jsonBody["result"].toObject();
        const QJsonArray versions = result["supportedVersions"].toArray();
        QVERIFY(!versions.isEmpty());

        QVERIFY(result["capabilities"].toObject().contains("tools"));
        QVERIFY(result["capabilities"].toObject().contains("resources"));

        // Modern framing applies here too — discover is a modern method.
        QCOMPARE(result["resultType"].toString(), QString("complete"));
        QVERIFY(result.contains("ttlMs"));
        // "public", unlike every other result. Discover's payload is the
        // server's identity, version list and instructions — identical for every
        // caller and filtered by nothing — which is why the spec's own example
        // marks it public. Everything a caller can influence stays private.
        QCOMPARE(result["cacheScope"].toString(), QString("public"));

        // A modern client never handshakes, so this is its only carrier for the
        // shot-citation rule every legacy client is told at initialize.
        QVERIFY(result["instructions"].toString().contains("date and time"));
    }

    // The advertised list is a PROMISE. Every version named must actually be
    // served, and no LEGACY version may appear — a client is told to pick from
    // this list for subsequent requests, and every subsequent request it makes
    // will be modern-shaped.
    void serverDiscoverAdvertisesOnlyServableModernVersions()
    {
        McpServer server;
        auto resp = sendHttp(server, "POST", modernBody("server/discover", {}, 21));
        const QJsonArray versions =
            resp.jsonBody["result"].toObject()["supportedVersions"].toArray();

        QVERIFY(!versions.isEmpty());
        for (const QJsonValue& v : versions) {
            const QString version = v.toString();
            QVERIFY2(McpServer::isModernProtocolVersion(version),
                     qPrintable(QStringLiteral("advertised a legacy version: %1").arg(version)));

            // Served, not merely listed: the same request under that version
            // must not come back as unsupported.
            auto probe = sendHttp(server, "POST", modernBody("server/discover", {}, 22, version));
            QVERIFY2(!probe.jsonBody.contains("error"),
                     qPrintable(QStringLiteral("advertised but not served: %1").arg(version)));
        }
    }

    // The route a client written against another server actually takes: invoke a
    // method directly, get UnsupportedProtocolVersionError, retry from the list
    // it carries. Testing only the up-front discovery path would leave the
    // likelier one uncovered.
    void unsupportedVersionErrorCarriesAListThatWorksOnRetry()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail", "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; }, "read");

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern tools/list"));
        auto refused = sendHttp(server, "POST",
                                modernBody("tools/list", {}, 23, QStringLiteral("2030-01-01")));
        const QJsonObject error = refused.jsonBody["error"].toObject();
        QCOMPARE(error["code"].toInt(), -32022);
        QCOMPARE(refused.statusCode, 400);

        const QJsonArray supported = error["data"].toObject()["supported"].toArray();
        QVERIFY(!supported.isEmpty());

        // Retry with the first offering, exactly as a client would.
        auto retried = sendHttp(server, "POST",
                                modernBody("tools/list", {}, 24, supported.first().toString()));
        QCOMPARE(retried.statusCode, 200);
        QVERIFY2(retried.jsonBody.contains("result"),
                 "the list a client is told to retry from must actually work");
    }

    // A modern-shaped request naming a LEGACY version is incoherent: that
    // revision has no per-request _meta, no server/discover, and a handshake
    // this request never performed. Serving it would invent semantics no
    // revision defines.
    void modernRequestNamingALegacyVersionIsRefused()
    {
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern tools/list"));
        auto resp = sendHttp(server, "POST",
                             modernBody("tools/list", {}, 25, QStringLiteral("2025-11-25")));
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32022);
        QCOMPARE(resp.statusCode, 400);
    }

    // server/discover does not exist for a legacy client — `initialize` is that
    // era's answer to the same question.
    void legacyEraHasNoServerDiscover()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("server/discover", {}, 26), sid);
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32601);
    }

    // `subscriptions/listen` is opt-in, and strictly: "the server MUST NOT send
    // notification types the client has not explicitly requested". The request
    // shape here matches the spec's own example payload
    // (schema/2026-07-28/examples/SubscriptionsListenRequest).
    void subscriptionsListenDeliversOnlyOptedInResources()
    {
        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://machine/state", "State", "d", "application/json",
            []() -> QJsonObject { return QJsonObject{}; });

        HeldConnection conn;
        QVERIFY(conn.open());

        QJsonObject filter;
        filter["resourceSubscriptions"] = QJsonArray{"decenza://machine/state"};
        QJsonObject args;
        args["notifications"] = filter;
        conn.send(server, modernBody("subscriptions/listen", args, 40));

        // The stream answers text/event-stream and does NOT complete — the
        // JSON-RPC response comes only on a graceful teardown.
        const QByteArray opened = conn.client.waitForReadyRead(1000)
                                      ? conn.client.readAll() : QByteArray();
        QVERIFY2(opened.contains("text/event-stream"), "listen must open a stream");
        QVERIFY2(!opened.contains("\"result\""),
                 "a listen request is not answered while the stream is live");

        // The acknowledgment MUST be the FIRST message, and nothing may precede
        // it. Without it a client cannot tell an established subscription from a
        // silent one, and the suite reads zero frames.
        QVERIFY2(opened.contains("notifications/subscriptions/acknowledged"),
                 "the ack must be the first frame on the stream");
        QVERIFY2(opened.contains("io.modelcontextprotocol/subscriptionId"),
                 "the ack carries the subscription id like every frame after it");

        // It reports what was AGREED, not what was asked. listChanged types are
        // omitted because this server can never send them — tools and resources
        // are registered once at startup — so promising them would leave a
        // client waiting forever.
        QVERIFY2(opened.contains("resourceSubscriptions"), "the honoured type is reported");
        QVERIFY2(!opened.contains("toolsListChanged"),
                 "a type the server cannot send must be omitted from the ack");

        // An opted-in resource arrives, tagged with the subscription id.
        server.notifyResourceChanged("decenza://machine/state");
        conn.client.waitForReadyRead(1000);
        const QByteArray event = conn.client.readAll();
        QVERIFY2(event.contains("notifications/resources/updated"), "opted-in URI must arrive");
        QVERIFY2(event.contains("io.modelcontextprotocol/subscriptionId"),
                 "every notification on a listen stream must carry its subscription id");
        QVERIFY2(event.contains("40"), "the subscription id is the listen request's id");

        // A resource NOT opted into must not arrive at all.
        server.notifyResourceChanged("decenza://profiles/active");
        conn.client.waitForReadyRead(200);
        QVERIFY2(!conn.client.readAll().contains("profiles/active"),
                 "the server MUST NOT send what the client did not ask for");
    }

    // The legacy GET stream and the two subscribe verbs are untouched — this era
    // reaches the same notifications the way it always did.
    void legacyEraHasNoSubscriptionsListen()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("subscriptions/listen", {}, 41), sid);
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32601);
    }

    // Resource-not-found is era-dependent from 2026-07-28: -32602 (Invalid
    // Params) there, -32002 for legacy. Both correct for the revision they are
    // sent under, which is why this is a fork rather than a change.
    void resourceNotFoundCodeIsEraDependent()
    {
        McpServer server;

        const QString sid = openSession(server, "2025-11-25");
        auto legacy = sendHttp(server, "POST",
                               rpcBody("resources/read",
                                       QJsonObject{{"uri", "decenza://nope"}}, 50), sid);
        QCOMPARE(legacy.jsonBody["error"].toObject()["code"].toInt(), -32002);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Refusing modern resources/read"));
        auto modern = sendHttp(server, "POST",
                               modernBody("resources/read",
                                          QJsonObject{{"uri", "decenza://nope"}}, 51));
        QCOMPARE(modern.jsonBody["error"].toObject()["code"].toInt(), -32602);
        QCOMPARE(modern.statusCode, 400);

        // The URI stays in `data` in both eras — it is what makes the error
        // actionable, and only the code moved.
        QCOMPARE(modern.jsonBody["error"].toObject()["data"].toObject()["uri"].toString(),
                 QString("decenza://nope"));
    }

    // `_meta` validation had no coverage at all, and could not have had any:
    // modernBody() always writes every required field, so none of the three
    // -32602 branches had ever executed under test. Conformance found them; the
    // commit that fixed them added no test, which is the same blindness twice.
    void modernMetaValidation_data()
    {
        QTest::addColumn<QJsonObject>("params");
        QTest::addColumn<bool>("expectRefusal");

        const QString v = QStringLiteral("2026-07-28");

        // No _meta at all. Reached because a modern-ONLY method is proof of era,
        // which is exactly why it must be validated rather than assumed.
        QTest::newRow("no _meta") << QJsonObject{} << true;

        QTest::newRow("no protocolVersion")
            << QJsonObject{{"_meta", QJsonObject{
                   {"io.modelcontextprotocol/clientCapabilities", QJsonObject{}}}}}
            << true;

        QTest::newRow("no clientCapabilities")
            << QJsonObject{{"_meta", QJsonObject{
                   {"io.modelcontextprotocol/protocolVersion", v}}}}
            << true;

        // clientInfo absent is VALID — spec PR #3002 demoted it to a SHOULD.
        // This is the negative case that most needs pinning: making it required
        // would break every conforming client and pass everything else.
        QTest::newRow("no clientInfo (legal)")
            << QJsonObject{{"_meta", QJsonObject{
                   {"io.modelcontextprotocol/protocolVersion", v},
                   {"io.modelcontextprotocol/clientCapabilities", QJsonObject{}}}}}
            << false;
    }

    void modernMetaValidation()
    {
        QFETCH(QJsonObject, params);
        QFETCH(bool, expectRefusal);

        McpServer server;
        QJsonObject req{{"jsonrpc", "2.0"}, {"id", 60},
                        {"method", "server/discover"}, {"params", params}};

        if (expectRefusal) {
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression("Refusing modern server/discover"));
        }
        auto resp = sendHttp(server, "POST",
                             QJsonDocument(req).toJson(QJsonDocument::Compact));

        if (expectRefusal) {
            QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32602);
            QCOMPARE(resp.statusCode, 400);
        } else {
            QCOMPARE(resp.statusCode, 200);
            QVERIFY2(resp.jsonBody.contains("result"),
                     "clientInfo is a SHOULD; requiring it would break conforming clients");
        }
    }

    // The era-detection rule itself, as opposed to its exception. A modern-only
    // method with NO session and NO _meta must be validated as modern (-32602),
    // not answered "method not found" — the exception (a live legacy session
    // gets -32601) is covered by legacyEraHasNoServerDiscover.
    void modernOnlyMethodWithoutSessionIsValidatedAsModern()
    {
        McpServer server;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Refusing modern subscriptions/listen"));
        QJsonObject req{{"jsonrpc", "2.0"}, {"id", 61},
                        {"method", "subscriptions/listen"}, {"params", QJsonObject{}}};
        auto resp = sendHttp(server, "POST",
                             QJsonDocument(req).toJson(QJsonDocument::Compact));

        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32602);
        QCOMPARE(resp.statusCode, 400);
    }

    // A deferred response must carry modern framing too. handleModernRequest
    // stamps only what it answers synchronously, and its comment claimed
    // otherwise — 22 async tools and every confirmation outcome went around it,
    // returning results the schema does not permit.
    void deferredModernResponseIsStillFramedAsModern()
    {
        McpServer server;
        server.toolRegistry()->registerAsyncTool(
            "shots_list", "An async tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&, std::function<void(QJsonObject)> respond) {
                respond(QJsonObject{{"ok", true}});
            },
            "read");

        QJsonObject params;
        params["name"] = "shots_list";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", modernBody("tools/call", params, 62));

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();
        QCOMPARE(result["resultType"].toString(), QString("complete"));
        QVERIFY2(result["_meta"].toObject().contains("io.modelcontextprotocol/serverInfo"),
                 "a deferred result is still a modern result");
    }

    // ─── Spec-version gating: the floor revision sees only its own fields ───
    //
    // A strict validator rejects a response carrying fields introduced after the
    // revision it negotiated. The server must omit those when the negotiated
    // version pre-dates their introduction. The tests below pin the contract at
    // the LOWEST supported revision, which is where a leak would show, so a
    // future spec bump cannot silently push a newer field down to it.
    //
    // The floor was 2024-11-05 until those revisions were dropped; it is now
    // 2025-06-18. Only the `icons` and `$schema` assertions below are gates —
    // `title` is emitted at every negotiable revision, so its presence proves
    // nothing about version handling and is no longer asserted here.

    void toolsListAtFloorRevisionOmitsNewerSpecFields()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        const QString sid = openSession(server, "2025-06-18");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), 1);
        const QJsonObject t = tools[0].toObject();

        // `title` is unconditional now — asserted here only so the record shows
        // it survives at the floor. Its exact value is pinned by
        // toolsListCarriesTitleAndSchemaButNoIcons; this is not a gate.
        // 2025-11-25 fields must NOT appear.
        QVERIFY2(!t.contains("icons"), "tools/list at 2025-06-18 must omit icons");
        QVERIFY2(!t["inputSchema"].toObject().contains("$schema"),
                 "tools/list at 2025-06-18 must omit $schema dialect");

        // Universal fields must still be present.
        QCOMPARE(t["name"].toString(), QString("shots_get_detail"));
        QVERIFY(t.contains("description"));
        QVERIFY(t["inputSchema"].toObject().contains("type"));
    }

    void resourcesListAtFloorRevisionOmitsNewerSpecFields()
    {
        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://shots/42", "Shot 42", "A test shot", "application/json",
            []() -> QJsonObject { return QJsonObject{{"id", 42}}; });

        const QString sid = openSession(server, "2025-06-18");
        auto resp = sendHttp(server, "POST", rpcBody("resources/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray resources = resp.jsonBody["result"].toObject()["resources"].toArray();
        QCOMPARE(resources.size(), 1);
        const QJsonObject r = resources[0].toObject();

        // `title` unconditional — see the tools/list sibling above.
        QVERIFY2(!r.contains("icons"), "resources/list at 2025-06-18 must omit icons");
        QCOMPARE(r["name"].toString(), QString("Shot 42"));
        QCOMPARE(r["uri"].toString(), QString("decenza://shots/42"));
    }

    // Symmetric presence at 2025-06-18: structuredContent and resource_link
    // were introduced in this revision. If the threshold ever drifts up to
    // 2025-11-25 (e.g. someone "matches the icons gate"), 2025-06-18 clients
    // would silently lose both. This test pins them at exactly 2025-06-18.
    void toolsCallAt2025_06_18EmitsStructuredContentAndResourceLinks()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "stub_listy_tool",
            "Returns _resourceLinks side-channel",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject {
                QJsonObject link{{"uri", "decenza://shots/42"}, {"title", "#42"}};
                return QJsonObject{{"items", QJsonArray{42}},
                                   {"_resourceLinks", QJsonArray{link}}};
            },
            "read");

        const QString sid = openSession(server, "2025-06-18");
        QJsonObject params;
        params["name"] = "stub_listy_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonObject result = resp.jsonBody["result"].toObject();
        QVERIFY2(result.contains("structuredContent"),
                 "tools/call at 2025-06-18 must emit structuredContent");
        const QJsonArray content = result["content"].toArray();
        bool hasResourceLink = false;
        for (const QJsonValue& v : content)
            if (v.toObject()["type"].toString() == "resource_link") { hasResourceLink = true; break; }
        QVERIFY2(hasResourceLink,
                 "tools/call at 2025-06-18 must emit resource_link blocks");
    }

    // Mirror toolsListIncludesTitleAndIcons for resources/list. Without a
    // 2025-11-25 presence test on the resource side, a regression that re-
    // introduced `"sizes":"any"` (bare string) on resource icons would be
    // invisible to CI even though it would zero-out resources for strict
    // clients exactly the way the tools-side bug did.
    void resourcesListAt2025_11_25EmitsTitleAndIcons()
    {
        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://shots/recent", "Recent Shots", "Test resource",
            "application/json", []() -> QJsonObject { return QJsonObject{}; });

        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("resources/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray resources = resp.jsonBody["result"].toObject()["resources"].toArray();
        QCOMPARE(resources.size(), 1);
        const QJsonObject r = resources[0].toObject();

        QVERIFY2(r.contains("title"),
                 "resources/list at 2025-06-18+ must include title");
        const QJsonArray icons = r["icons"].toArray();
        QVERIFY2(!icons.isEmpty(),
                 "resources/list at 2025-11-25 must include at least one icon");
        const QJsonObject icon = icons[0].toObject();
        // Same icon.sizes shape pin as the tools-side test.
        const QJsonValue sizes = icon["sizes"];
        QVERIFY2(sizes.isArray(), "resource icon.sizes must be an array (string[])");
        QVERIFY(!sizes.toArray().isEmpty());
        QVERIFY(sizes.toArray()[0].isString());
    }

    // A resource family with no icon of its own — iconQrcForResource() answers
    // that with an empty path — must not be discovered by TRYING TO OPEN it.
    // QFile("").open() takes the filesystem engine and Qt prints
    // "QFSFileEngine::open: No file name specified"
    // (qtbase/src/corelib/io/qfsfileengine.cpp:204), once per iconless resource
    // per resources/list. On the shipped tablet build that is the 15
    // decenza://tools/<topic> docs resources: 15 warning lines every time an MCP
    // client connects, in the ring buffer field diagnosis reads. init()'s
    // failOnWarning() is what asserts the absence — dropping the empty-path guard
    // in iconDataUri() turns this test red.
    void iconlessResourceFamilyEmitsNoIconsAndOpensNoFile()
    {
        QVERIFY2(McpRegistryHelpers::iconQrcForResource("decenza://tools/debug_get_log").isEmpty(),
                 "the tool-docs family is expected to map to no icon; if it gained one, "
                 "this test needs a different iconless URI, not deleting");

        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://tools/debug_get_log", "debug_get_log documentation",
            "Long-form documentation for the debug_get_log MCP tool",
            "application/json", []() -> QJsonObject { return QJsonObject{}; });

        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("resources/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray resources = resp.jsonBody["result"].toObject()["resources"].toArray();
        QCOMPARE(resources.size(), 1);
        QVERIFY2(!resources[0].toObject().contains("icons"),
                 "an iconless family must omit `icons` entirely, not ship an empty array");
    }

    // `structuredContent` is not a field of `ResourceContents` in ANY MCP
    // revision — it exists on `CallToolResult` alone. This used to assert the
    // omission at 2024-11-05 only, which encoded the mistaken premise that it
    // was a 2025-06-18 resources feature being gated. Every version, and the
    // payload must still be fully readable from `text`.
    void resourcesReadOmitsStructuredContent_data()
    {
        QTest::addColumn<QString>("version");
        QTest::newRow("2025-06-18") << "2025-06-18";
        QTest::newRow("2025-11-25") << "2025-11-25";
    }

    void resourcesReadOmitsStructuredContent()
    {
        QFETCH(QString, version);

        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://shots/42", "Shot 42", "A test shot", "application/json",
            []() -> QJsonObject { return QJsonObject{{"id", 42}, {"label", "ok"}}; });

        const QString sid = openSession(server, version);
        QJsonObject params;
        params["uri"] = "decenza://shots/42";
        auto resp = sendHttp(server, "POST", rpcBody("resources/read", params, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray contents = resp.jsonBody["result"].toObject()["contents"].toArray();
        QCOMPARE(contents.size(), 1);
        const QJsonObject content = contents[0].toObject();

        QVERIFY2(!content.contains("structuredContent"),
                 "structuredContent is not a ResourceContents field at any version");
        QCOMPARE(content["uri"].toString(), QString("decenza://shots/42"));

        // The payload is not lost by the removal — `text` still carries it.
        const QJsonObject payload =
            QJsonDocument::fromJson(content["text"].toString().toUtf8()).object();
        QCOMPARE(payload["id"].toInt(), 42);
        QCOMPARE(payload["label"].toString(), QString("ok"));
    }

    // ─── Spec conformance: batches, terminated sessions, error codes, SSE ──

    // Batching is defined by exactly one revision, 2025-03-26, which this server
    // no longer serves; the dispatch for it is gone. An array parses fine, so
    // without an explicit refusal it would fall through to `doc.object()` and be
    // answered "method not found" — true of the empty object it became, and
    // useless to whoever sent the batch.
    void batchedRequestIsRefusedExplicitly()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        // A side-effect-recording tool, because "refused" and "refused after
        // running anyway" are indistinguishable from a batch of ping and
        // tools/list. That distinction is not academic: the batch path once
        // deleted a shot and then told the client the call had been refused.
        auto dispatched = std::make_shared<bool>(false);
        server.toolRegistry()->registerTool(
            "stub_side_effect",
            "Records that it ran",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [dispatched](const QJsonObject&) -> QJsonObject {
                *dispatched = true;
                return QJsonObject{{"ok", true}};
            },
            "read");

        QJsonObject callParams;
        callParams["name"] = "stub_side_effect";
        callParams["arguments"] = QJsonObject{};

        QJsonArray batch;
        batch.append(QJsonDocument::fromJson(rpcBody("ping", {}, 11)).object());
        batch.append(QJsonDocument::fromJson(rpcBody("tools/call", callParams, 12)).object());

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Refused a JSON-RPC batch of 2 "
                                                "\\(ping, tools/call\\)"));
        auto resp = sendHttp(server, "POST",
                             QJsonDocument(batch).toJson(QJsonDocument::Compact), sid);

        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32600);
        QVERIFY(resp.jsonBody["error"].toObject()["message"].toString()
                    .contains("not supported"));
        // Null id per JSON-RPC 2.0: no single id can be attributed to a batch.
        QVERIFY(resp.jsonBody["id"].isNull());

        // The assertion that makes this test worth its build time: the refusal
        // happens BEFORE dispatch. Move the isArray() check after a dispatch
        // loop and only this line reddens.
        QVERIFY2(!*dispatched,
                 "a batched tool must not run — refusing it after it has already "
                 "taken effect is not refusing it");

        // Exactly one HTTP response on the socket. A second complete body
        // desyncs a keep-alive connection rather than merely confusing a parse.
        QVERIFY2(!resp.rawBody.contains("HTTP/1.1"),
                 "a second complete HTTP response was written into the body");
    }

    // 202-with-no-body for a notification is live production code exercised by
    // every openSession() in this file, but the response was never asserted —
    // the only test that pinned the shape was a batch test, deleted with
    // batching. A regression to 200-with-body or 204 would pass everything else.
    void notificationAnswersWith202AndNoBody()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        auto resp = sendHttp(server, "POST", notifyBody("notifications/initialized"), sid);
        QCOMPARE(resp.statusCode, 202);
        QVERIFY(resp.rawBody.isEmpty());
    }

    // The default McpSession protocol version, which nothing observed. Its
    // comment argues a deliberate spec deviation justified by a safety property
    // — that such a session is under-served rather than over-served — and that
    // property had no assertion: setting the default to 2025-11-25 passed the
    // whole suite while silently sending 2025-11-25 fields to a session that
    // never negotiated.
    //
    // Driven through auto-create with NO version header, so the adoption branch
    // cannot fire and the default is what remains.
    void autoCreatedSessionWithoutHeaderUsesTheFloorRevision()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        // Two live sessions so the "reuse the sole session" branch cannot fire.
        QVERIFY(!openSession(server, "2025-11-25").isEmpty());
        QVERIFY(!openSession(server, "2025-11-25").isEmpty());

        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 5),
                             "00000000-0000-0000-0000-000000000000");
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), 1);
        QVERIFY2(!tools.first().toObject()["inputSchema"].toObject().contains("$schema"),
                 "a session that never negotiated must not receive 2025-11-25 fields");
    }

    void singleObjectPostStillAnswersWithSingleObject()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        auto resp = sendHttp(server, "POST", rpcBody("ping", {}, 31), sid);
        QCOMPARE(resp.statusCode, 200);
        QVERIFY2(resp.jsonArrayBody.isEmpty(),
                 "a single-message POST must not start answering with an array");
        QCOMPARE(resp.jsonBody["id"].toInt(), 31);
    }

    void terminatedSessionReturns404()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        auto del = sendHttp(server, "DELETE", "", sid);
        QCOMPARE(del.statusCode, 200);

        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 41), sid);
        QCOMPARE(resp.statusCode, 404);
    }

    // The 404 above must not swallow the auto-recovery path: an ID the server
    // never issued is indistinguishable from one it issued before a restart, and
    // per-request re-initializing clients depend on being served anyway. Two
    // sessions exist so the "reuse the sole session" shortcut is not what passes
    // this.
    void unrecognizedSessionIsStillServed()
    {
        McpServer server;
        openSession(server, "2025-11-25");
        openSession(server, "2025-11-25");

        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 42),
                             "never-issued-session-id");
        QCOMPARE(resp.statusCode, 200);
        QVERIFY(resp.jsonBody.contains("result"));
    }

    // Re-initializing is the documented move after a 404, so `initialize` is the
    // one method a terminated ID may still carry.
    void initializeWithTerminatedSessionIdIsAccepted()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        sendHttp(server, "DELETE", "", sid);

        QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params, 43), sid);
        QCOMPARE(resp.statusCode, 200);
        QVERIFY(resp.jsonBody.contains("result"));
    }

    void unknownResourceReturnsResourceNotFoundCode()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        QJsonObject params;
        params["uri"] = "decenza://nonexistent";
        auto resp = sendHttp(server, "POST", rpcBody("resources/read", params, 51), sid);

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject error = resp.jsonBody["error"].toObject();
        QCOMPARE(error["code"].toInt(), -32002);
        QCOMPARE(error["data"].toObject()["uri"].toString(), QString("decenza://nonexistent"));
    }

    void sseStreamPrimesReconnection()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        auto resp = sendHttp(server, "GET", "", sid,
                             {{"Accept", "text/event-stream"}});
        QCOMPARE(resp.statusCode, 200);

        // Anchored, not `contains`: a bare contains("id:") would be satisfied by
        // an "id:" appearing anywhere, including inside a payload.
        QVERIFY2(resp.rawBody.startsWith("retry: 3000\n"),
                 qPrintable("stream must open with the retry interval, got: " + resp.rawBody));
        QVERIFY2(QRegularExpression("\\bid: \\d+\\n").match(resp.rawBody).hasMatch(),
                 "the priming event must carry a numeric event ID");

        // NO `data` field, deliberately. Per the HTML SSE processing model a
        // `data` field appends its value plus a newline, so `data: \n\n` leaves a
        // non-empty buffer and DISPATCHES a message event carrying "" — which
        // every MCP client JSON.parses, and which therefore throws on every
        // stream open. A field-less block sets the last event ID and dispatches
        // nothing, which is what priming means.
        QVERIFY2(!resp.rawBody.contains("data:"),
                 "the priming event must NOT carry a data field — it would dispatch an "
                 "empty message the client tries to parse");
    }

    // A terminated session must 404 on GET too, not only POST. This is the verb a
    // client reaches for FIRST after losing a session, and serving it hands back
    // a stream that can never carry an event for that session — so the client
    // waits forever instead of learning to re-initialize.
    void terminatedSessionReturns404OnSseGet()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        sendHttp(server, "DELETE", "", sid);

        auto resp = sendHttp(server, "GET", "", sid, {{"Accept", "text/event-stream"}});
        QCOMPARE(resp.statusCode, 404);
    }

    void terminatedSessionReturns404OnRepeatedDelete()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        QCOMPARE(sendHttp(server, "DELETE", "", sid).statusCode, 200);
        QCOMPARE(sendHttp(server, "DELETE", "", sid).statusCode, 404);
    }

    // The array-refusal and parse-error paths must answer with `id: null`, not
    // `id: 0`. QJsonValue::Null is an unscoped enumerator of value 0, so passing
    // it where a QVariant is expected picks QVariant(int) over
    // QVariant(const QJsonValue&) — an integral promotion beats a user-defined
    // conversion — and 0 is a legal id a client may correlate against a real
    // request.
    void unidentifiableRequestsAnswerWithNullId()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Refused a JSON-RPC batch of 0"));
        auto empty = sendHttp(server, "POST", "[]", sid);
        QVERIFY2(empty.jsonBody["id"].isNull(), "refused array must answer id: null");

        auto garbage = sendHttp(server, "POST", "{not json}", sid);
        QVERIFY2(garbage.jsonBody["id"].isNull(), "parse error must answer id: null");
    }

    // An access-level refusal is a server-side policy decision the caller cannot
    // fix by changing its arguments, so it stays -32603. Pinned separately from
    // the unknown-tool case because a "simplification" that mapped every registry
    // failure to -32602 would tell a read-only client its ARGUMENTS were wrong,
    // and a model would then rewrite them forever.
    void accessDeniedStaysInternalError()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "stub_control_tool", "Needs a higher access level",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"success", true}}; },
            "control");

        // No Settings wired, so mcpAccessLevel() is 0 — below "control".
        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "stub_control_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 81), sid);

        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32603);
    }

    // ─── Merged (action-dispatch) tools ────────────────────────────────────
    //
    // A merged tool has one name and several verbs with different answers to
    // "may this caller run it?" and "must a human confirm it first?". Both used
    // to be looked up by tool NAME, which would have collapsed every verb onto
    // the family's most dangerous member. These four tests pin the resolution,
    // including the case the design turns on: an `action` the server cannot
    // resolve is gated as the strictest verb, not waved through as a read.
    void mergedToolResolvesAccessLevelPerAction()
    {
        McpServer server;
        registerStubActionsOn(server.toolRegistry());

        // No Settings wired, so mcpAccessLevel() is 0 (Monitor).
        const QString sid = openSession(server, "2025-11-25");

        QCOMPARE(callStubAction(server, sid, QJsonObject{{"action", "list"}})
                     .jsonBody["error"].toObject()["code"].toInt(), 0);
        QCOMPARE(callStubAction(server, sid, QJsonObject{{"action", "erase"}})
                     .jsonBody["error"].toObject()["code"].toInt(), -32603);
    }

    void mergedToolWithUnresolvableActionIsGatedAsTheStrictestVerb()
    {
        McpServer server;
        registerStubActionsOn(server.toolRegistry());
        const QString sid = openSession(server, "2025-11-25");

        // Monitor level. Omitting `action` must NOT resolve to the read verb.
        QCOMPARE(callStubAction(server, sid, QJsonObject{})
                     .jsonBody["error"].toObject()["code"].toInt(), -32603);
        QCOMPARE(callStubAction(server, sid, QJsonObject{{"action", "frobnicate"}})
                     .jsonBody["error"].toObject()["code"].toInt(), -32603);
    }

    void mergedToolReportsUnknownActionWithTheValidOnes()
    {
        McpToolRegistry registry;
        registerStubActionsOn(&registry);

        QString err;
        QJsonObject out;
        QVERIFY(registry.callAsyncTool("stub_family", QJsonObject{{"action", "frobnicate"}}, 2, err,
                                       [&out](QJsonObject r) { out = r; }));
        const QString message = out["error"].toString();
        QVERIFY2(message.contains("frobnicate"), qPrintable(message));
        QVERIFY2(message.contains("list") && message.contains("erase"), qPrintable(message));

        QVERIFY(registry.callAsyncTool("stub_family", QJsonObject{}, 2, err,
                                       [&out](QJsonObject r) { out = r; }));
        QVERIFY2(out["error"].toString().contains("requires an `action`"),
                 qPrintable(out["error"].toString()));
    }

    void mergedToolConfirmationIsPerActionAndNamesTheVerb()
    {
        McpToolRegistry registry;
        registerStubActionsOn(&registry);

        QVERIFY(!registry.confirmationFor("stub_family", QJsonObject{{"action", "list"}}).required);

        const McpConfirmationRequirement erase =
            registry.confirmationFor("stub_family", QJsonObject{{"action", "erase"}});
        QVERIFY(erase.required);
        QCOMPARE(erase.actionId, QString("stub_family.erase"));
        QCOMPARE(erase.description, QString("Erase the stub"));

        // Fail closed: no action named, but the tool has a confirmable verb.
        QVERIFY(registry.confirmationFor("stub_family", QJsonObject{}).required);
    }

    // The three things McpServer DOES with the registry's per-action answers. The
    // four tests above pin the resolution; these pin its consumption, which is
    // where the change actually lives — every line this added to handleToolsCall
    // could be reverted with the tests above still green.
    void mergedToolReadActionDoesNotSpendTheControlRateLimit()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(0);
        server.setSettings(&settings);
        registerStubActionsOn(server.toolRegistry());

        const QString sid = openSession(server, "2025-11-25");
        // Comfortably past the 60/min control budget. A read verb must not touch it;
        // resolving the category from the tool NAME (which reports the strictest verb
        // for a merged tool) would start refusing routine reads at call 61.
        for (int i = 0; i < McpServer::RateLimitPerMinute + 10; ++i) {
            auto resp = callStubAction(server, sid, QJsonObject{{"action", "list"}});
            QVERIFY2(resp.jsonBody["error"].toObject()["code"].toInt() != -32000,
                     qPrintable(QStringLiteral("read verb rate-limited at call %1").arg(i + 1)));
        }
    }

    void mergedToolWriteActionSpendsTheControlRateLimit()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(0);
        server.setSettings(&settings);
        registerStubActionsOn(server.toolRegistry());

        const QString sid = openSession(server, "2025-11-25");
        // Exactly at the budget, so the refusal lands on a known call and its (single,
        // deliberate) warning can be consumed — this suite fails on an unexpected one.
        for (int i = 0; i < McpServer::RateLimitPerMinute; ++i)
            QCOMPARE(callStubAction(server, sid, QJsonObject{{"action", "erase"}})
                         .jsonBody["error"].toObject()["code"].toInt(), 0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Rate limit exceeded"));
        QCOMPARE(callStubAction(server, sid, QJsonObject{{"action", "erase"}})
                     .jsonBody["error"].toObject()["code"].toInt(), -32000);
    }

    void mergedToolConfirmationPayloadNamesTheVerbOverHttp()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(1);
        server.setSettings(&settings);
        registerStubActionsOn(server.toolRegistry());

        const QString sid = openSession(server, "2025-11-25");

        const QString erase = confirmationPayloadText(
            callStubAction(server, sid, QJsonObject{{"action", "erase"}}));
        QVERIFY2(erase.contains("needs_confirmation"), qPrintable(erase));
        QVERIFY2(erase.contains("stub_family.erase"), qPrintable(erase));
        QVERIFY2(erase.contains("Erase the stub"), qPrintable(erase));

        // A read verb is not confirmed, and an unresolvable one is — through the
        // server, not just the registry.
        QVERIFY(!confirmationPayloadText(
                     callStubAction(server, sid, QJsonObject{{"action", "list"}}))
                     .contains("needs_confirmation"));
        QVERIFY(confirmationPayloadText(callStubAction(server, sid, QJsonObject{}))
                    .contains("needs_confirmation"));
    }

    // Starting the machine is confirmed on the machine's own screen, and NOT a
    // second time in chat. Both halves are one string equality apart from silence:
    // if `needsInAppConfirmation`'s name stops matching the registration, a network
    // client starts the machine with no confirmation of any kind.
    // The confirmation gate carries its OWN handle, not a session id. That is
    // what lets a stateless caller hold one: the value was never looked up as a
    // session, it was only ever echoed back.
    void confirmationHandleIsNotASessionId()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(1);
        server.setSettings(&settings);
        server.toolRegistry()->registerTool(
            "machine_start", "stub",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"success", true}}; },
            "control");

        QSignalSpy spy(&server, &McpServer::confirmationRequested);
        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "machine_start";
        params["arguments"] = QJsonObject{{"action", "espresso"}};
        HeldConnection conn;
        QVERIFY(conn.open());
        conn.send(server, rpcBody("tools/call", params, 92), sid);

        QCOMPARE(spy.count(), 1);
        const QString handle = spy.at(0).at(2).toString();
        QVERIFY2(!handle.isEmpty(), "the UI needs something to echo back");
        QVERIFY2(handle != sid,
                 "the handle must be the confirmation's own identity, not the session's — "
                 "borrowing the session id is what excluded stateless callers");

        // The confirmation is still pending, and HeldConnection's destructor
        // closes the socket at scope exit — inside this test function — so the
        // backstop fires here. Expected rather than suppressed: it is the
        // behaviour under test in the sibling case.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Pending confirmation for machine_start "
                                                "abandoned — its connection closed"));
    }

    // A MODERN caller can hold a confirmation, which is the point of giving the
    // gate its own handle. Previously refused outright.
    void modernCallerCanHoldAConfirmation()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(1);
        server.setSettings(&settings);
        auto ran = std::make_shared<bool>(false);
        server.toolRegistry()->registerTool(
            "machine_start", "stub",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [ran](const QJsonObject&) -> QJsonObject {
                *ran = true;
                return QJsonObject{{"success", true}};
            },
            "control");

        QSignalSpy spy(&server, &McpServer::confirmationRequested);
        QJsonObject params;
        params["name"] = "machine_start";
        params["arguments"] = QJsonObject{{"action", "espresso"}};

        // Held open: a confirmation is a client still waiting on a deferred
        // response, and the server abandons one whose connection has gone.
        HeldConnection conn;
        QVERIFY(conn.open());
        conn.send(server, modernBody("tools/call", params, 93));

        QCOMPARE(spy.count(), 1);
        QVERIFY2(!*ran, "the tool must wait for the answer, not run and then ask");

        // Answering with the handle runs it — the round trip a stateless caller
        // could not previously complete.
        server.confirmationResolved(spy.at(0).at(2).toString(), true);
        QVERIFY2(*ran, "a confirmed modern call must actually dispatch");
    }

    // A confirmation whose requester has gone cannot be meaningfully answered.
    // This is the ONLY backstop the modern era can have — no session, so no idle
    // reaper will ever come for it — and for legacy it replaces "noticed up to
    // thirty minutes later" with "noticed immediately".
    void confirmationIsAbandonedWhenTheConnectionDrops()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(1);
        server.setSettings(&settings);
        auto ran = std::make_shared<bool>(false);
        server.toolRegistry()->registerTool(
            "machine_start", "stub",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [ran](const QJsonObject&) -> QJsonObject {
                *ran = true;
                return QJsonObject{{"success", true}};
            },
            "control");

        QSignalSpy spy(&server, &McpServer::confirmationRequested);
        QJsonObject params;
        params["name"] = "machine_start";
        params["arguments"] = QJsonObject{{"action", "espresso"}};
        // Expected BEFORE the send: sendHttp closes its socket on return, so the
        // backstop fires inside that call, not after it.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Pending confirmation for machine_start "
                                                "abandoned — its connection closed"));

        // sendHttp deliberately, NOT HeldConnection: its socket dies on return,
        // which is the very condition under test.
        sendHttp(server, "POST", modernBody("tools/call", params, 94));
        QCOMPARE(spy.count(), 1);

        // Answering a confirmation that is already gone must do nothing at all.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("confirmationResolved for .* but nothing is "
                                                "pending"));
        server.confirmationResolved(spy.at(0).at(2).toString(), true);
        QVERIFY2(!*ran,
                 "a confirmation whose requester is gone must not still be answerable");
    }

    void machineStartConfirmsInAppAndNotInChat()
    {
        McpServer server;
        Settings settings;
        settings.mcp()->setMcpAccessLevel(2);
        settings.mcp()->setMcpConfirmationLevel(1);
        server.setSettings(&settings);
        server.toolRegistry()->registerTool(
            "machine_start", "stub",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"success", true}}; },
            "control");

        QSignalSpy spy(&server, &McpServer::confirmationRequested);
        const QString sid = openSession(server, "2025-11-25");
        QJsonObject params;
        params["name"] = "machine_start";
        params["arguments"] = QJsonObject{{"action", "espresso"}};
        // Held open — see HeldConnection. Previously this used sendHttp, whose
        // socket dies on return; that was invisible until the gate gained a
        // socket-disconnect backstop, at which point this test was asserting
        // against a confirmation the server had already abandoned.
        HeldConnection conn;
        QVERIFY(conn.open());
        conn.send(server, rpcBody("tools/call", params, 91), sid);
        auto resp = readHeld(conn);

        QCOMPARE(spy.count(), 1);
        QVERIFY2(!confirmationPayloadText(resp).contains("needs_confirmation"),
                 "the in-app dialog owns this tool; a chat prompt as well is a double prompt");

        // The confirmation is still pending, and HeldConnection's destructor
        // closes the socket at scope exit — inside this test function — so the
        // backstop fires here. Expected rather than suppressed: it is the
        // behaviour under test in the sibling case.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Pending confirmation for machine_start "
                                                "abandoned — its connection closed"));
    }

    // ─── Pure helpers ──────────────────────────────────────────────────────

    // The tool-call response body as text, for asserting on the confirmation
    // payload without caring which content block carries it at which protocol
    // version.
    static QString confirmationPayloadText(const HttpResponse& resp)
    {
        return QString::fromUtf8(QJsonDocument(resp.jsonBody).toJson(QJsonDocument::Compact));
    }

    static void registerStubActionsOn(McpToolRegistry* registry)
    {
        registry->registerActionTool(
            "stub_family", "A read verb and a destructive one",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            QVector<McpToolAction>{
                McpRegistryHelpers::syncAction("list", "read",
                    [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"ok", true}}; }),
                McpRegistryHelpers::syncAction("erase", "settings",
                    [](const QJsonObject&) -> QJsonObject { return QJsonObject{{"ok", true}}; },
                    QStringLiteral("Erase the stub")),
            });
    }

    HttpResponse callStubAction(McpServer& server, const QString& sid, const QJsonObject& args)
    {
        QJsonObject params;
        params["name"] = "stub_family";
        params["arguments"] = args;
        return sendHttp(server, "POST", rpcBody("tools/call", params, 90), sid);
    }


    void deriveTitleProducesTitleCaseFromSnakeCase()
    {
        QCOMPARE(McpRegistryHelpers::deriveTitle("scale_tare"),
                 QString("Scale Tare"));
        QCOMPARE(McpRegistryHelpers::deriveTitle("machine_get_state"),
                 QString("Machine Get State"));
        QCOMPARE(McpRegistryHelpers::deriveTitle("simple"),
                 QString("Simple"));
        // Edge: empty input → empty output.
        QCOMPARE(McpRegistryHelpers::deriveTitle(QString()),
                 QString());
    }

    void withJsonSchemaDialectStampsSchemaWhenMissing()
    {
        QJsonObject in{{"type", "object"}, {"properties", QJsonObject{}}};
        const QJsonObject out = McpRegistryHelpers::withJsonSchemaDialect(in);
        QCOMPARE(out["$schema"].toString(),
                 QString("https://json-schema.org/draft/2020-12/schema"));
        QCOMPARE(out["type"].toString(), QString("object"));
    }

    void withJsonSchemaDialectIsIdempotentWhenSchemaPresent()
    {
        QJsonObject in{{"$schema", "https://example.com/custom"},
                       {"type", "object"}};
        const QJsonObject out = McpRegistryHelpers::withJsonSchemaDialect(in);
        // Existing $schema is preserved — we never override an explicit one.
        QCOMPARE(out["$schema"].toString(), QString("https://example.com/custom"));
    }
};

QTEST_MAIN(tst_McpServerProtocol)
#include "tst_mcpserver_protocol.moc"
