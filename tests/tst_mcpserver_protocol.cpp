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
void registerPresetsTools(McpToolRegistry*, Settings*, MainController*, MachineState*) {}
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
        QJsonArray jsonArrayBody;    // populated instead when the body is a JSON array (batch)
        QString rawBody;
        QString sessionId;
        QString protocolVersion;     // value of MCP-Protocol-Version response header
        QString allowOrigin;         // value of Access-Control-Allow-Origin
    };

    // Fire one HTTP request at the server. Extra request headers (Origin,
    // MCP-Protocol-Version, etc.) are appended raw — caller controls casing.
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
        const QByteArray raw = client.readAll();

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
        QTest::newRow("twoBack")   << "2025-03-26" << "2025-03-26";
        QTest::newRow("legacy")    << "2024-11-05" << "2024-11-05";
        // Unsupported → server returns its preferred version (first in list).
        QTest::newRow("ancient")   << "2023-01-01" << "2025-11-25";
    }

    void initializeNegotiatesRequestedVersion()
    {
        QFETCH(QString, requested);
        QFETCH(QString, expected);

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

    // The server-level `instructions` field (#1162) was introduced in MCP
    // revision 2025-03-26. It must be present once the negotiated version is
    // >= 2025-03-26 and absent for strict 2024-11-05 clients, which reject
    // initialize responses carrying unknown fields (same gating discipline
    // as structuredContent / resource_link in buildToolCallResponse).
    void initializeInstructionsGatedByVersion_data()
    {
        QTest::addColumn<QString>("requested");
        QTest::addColumn<bool>("expectInstructions");
        QTest::newRow("current")  << "2025-11-25" << true;
        QTest::newRow("prior")    << "2025-06-18" << true;
        QTest::newRow("twoBack")  << "2025-03-26" << true;   // revision that introduced `instructions`
        QTest::newRow("legacy")   << "2024-11-05" << false;  // pre-2025-03-26 → field omitted
        QTest::newRow("ancient")  << "2023-01-01" << true;   // unsupported → negotiates 2025-11-25
    }

    void initializeInstructionsGatedByVersion()
    {
        QFETCH(QString, requested);
        QFETCH(bool, expectInstructions);

        McpServer server;
        QJsonObject params{
            {"protocolVersion", requested},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1"}}}};
        auto resp = sendHttp(server, "POST", rpcBody("initialize", params));

        QCOMPARE(resp.statusCode, 200);
        const QJsonObject result = resp.jsonBody["result"].toObject();
        QCOMPARE(result.contains("instructions"), expectInstructions);
        if (expectInstructions) {
            const QString instr = result["instructions"].toString();
            QVERIFY2(!instr.isEmpty(),
                     "instructions must be a non-empty string when emitted");
            QVERIFY2(instr.contains(QStringLiteral("date and time")),
                     "instructions must carry the shot date/time citation rule (#1162)");
        }
    }

    // ─── MCP-Protocol-Version request header validation ────────────────────

    void protocolVersionHeaderMismatchReturns400()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());

        // The refusal is logged at WARN: the assistant is told, the user is not,
        // so the log line is the only thing that explains the silence. Expected
        // here rather than tolerated — init()'s failOnWarning() would otherwise
        // fail this test for the server doing exactly what it should.
        QTest::ignoreMessage(QtWarningMsg,
                             "[MCP][Server] Protocol version mismatch — header 2024-11-05, "
                             "session 2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid,
                             {{"MCP-Protocol-Version", "2024-11-05"}});

        QCOMPARE(resp.statusCode, 400);
        QVERIFY(resp.rawBody.contains("Protocol version mismatch"));
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

    void protocolVersionHeaderAbsentAccepted()
    {
        // Spec says clients pre-dating the requirement may omit the header;
        // server defaults the session to 2025-03-26 in that window.
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 99), sid);
        QCOMPARE(resp.statusCode, 200);
    }

    void autoRecoveredSessionAdoptsClientProtocolVersion()
    {
        // Regression for the lockout where a stale Mcp-Session-Id forced the
        // server to auto-create a new session, which kept the default
        // 2025-03-26 protocol version. The very next protocol-version-mismatch
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

        // Backward-compat: text content block always present for 2025-03-26
        // clients that don't read structuredContent.
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

    // Run at every negotiated version, not just the newest. The marking reads the
    // RAW payload, three lines below `if (emitStructured) structuredContent = …`,
    // and the tempting "simplification" is to test structuredContent for the key
    // instead. That key exists only at >= 2025-06-18, so such a refactor would
    // drop isError for 2024-11-05 and 2025-03-26 clients — with the whole suite
    // green, because isError has been in CallToolResult since 2024-11-05 and no
    // other test calls a FAILING tool at a legacy version.
    void toolsCallMarksErrorPayloadAsFailed_data()
    {
        QTest::addColumn<QString>("protocolVersion");
        QTest::newRow("2024-11-05") << "2024-11-05";
        QTest::newRow("2025-03-26") << "2025-03-26";
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

    // ─── Spec-version gating: 2024-11-05 clients see only legacy fields ───
    //
    // Strict 2024-11-05 validators reject responses carrying fields introduced
    // in newer specs. The server must omit those fields when the negotiated
    // protocol version pre-dates their introduction. Each test below pins the
    // exact field contract for the legacy version so a future spec bump can't
    // silently re-leak a newer field.

    void toolsListAt2024_11_05OmitsNewerSpecFields()
    {
        McpServer server;
        server.toolRegistry()->registerTool(
            "shots_get_detail",
            "Test tool",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [](const QJsonObject&) -> QJsonObject { return QJsonObject{}; },
            "read");

        const QString sid = openSession(server, "2024-11-05");
        auto resp = sendHttp(server, "POST", rpcBody("tools/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray tools = resp.jsonBody["result"].toObject()["tools"].toArray();
        QCOMPARE(tools.size(), 1);
        const QJsonObject t = tools[0].toObject();

        // 2025-06-18 fields must NOT appear.
        QVERIFY2(!t.contains("title"), "tools/list at 2024-11-05 must omit title");
        // 2025-11-25 fields must NOT appear.
        QVERIFY2(!t.contains("icons"), "tools/list at 2024-11-05 must omit icons");
        QVERIFY2(!t["inputSchema"].toObject().contains("$schema"),
                 "tools/list at 2024-11-05 must omit $schema dialect");

        // Universal fields must still be present.
        QCOMPARE(t["name"].toString(), QString("shots_get_detail"));
        QVERIFY(t.contains("description"));
        QVERIFY(t["inputSchema"].toObject().contains("type"));
    }

    void resourcesListAt2024_11_05OmitsNewerSpecFields()
    {
        McpServer server;
        server.resourceRegistry()->registerResource(
            "decenza://shots/42", "Shot 42", "A test shot", "application/json",
            []() -> QJsonObject { return QJsonObject{{"id", 42}}; });

        const QString sid = openSession(server, "2024-11-05");
        auto resp = sendHttp(server, "POST", rpcBody("resources/list", {}, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonArray resources = resp.jsonBody["result"].toObject()["resources"].toArray();
        QCOMPARE(resources.size(), 1);
        const QJsonObject r = resources[0].toObject();

        QVERIFY2(!r.contains("title"), "resources/list at 2024-11-05 must omit title");
        QVERIFY2(!r.contains("icons"), "resources/list at 2024-11-05 must omit icons");
        QCOMPARE(r["name"].toString(), QString("Shot 42"));
        QCOMPARE(r["uri"].toString(), QString("decenza://shots/42"));
    }

    void toolsCallAt2024_11_05OmitsStructuredContentAndResourceLinks()
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

        const QString sid = openSession(server, "2024-11-05");
        QJsonObject params;
        params["name"] = "stub_listy_tool";
        params["arguments"] = QJsonObject{};
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 2), sid);
        QCOMPARE(resp.statusCode, 200);

        const QJsonObject result = resp.jsonBody["result"].toObject();

        // 2025-06-18 fields must NOT appear at 2024-11-05.
        QVERIFY2(!result.contains("structuredContent"),
                 "tools/call at 2024-11-05 must omit structuredContent");

        // content[] must still carry the text block, but no resource_link blocks.
        const QJsonArray content = result["content"].toArray();
        QVERIFY2(!content.isEmpty(), "content[] must always be present");
        bool hasText = false;
        for (const QJsonValue& v : content) {
            const QString type = v.toObject()["type"].toString();
            QVERIFY2(type != "resource_link",
                     "tools/call at 2024-11-05 must not emit resource_link blocks");
            if (type == "text") hasText = true;
        }
        QVERIFY(hasText);
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
        QTest::newRow("2024-11-05") << "2024-11-05";
        QTest::newRow("2025-03-26") << "2025-03-26";
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

    void batchOfTwoRequestsReturnsArrayOfResponses()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        QJsonArray batch;
        batch.append(QJsonDocument::fromJson(rpcBody("ping", {}, 11)).object());
        batch.append(QJsonDocument::fromJson(rpcBody("tools/list", {}, 12)).object());

        auto resp = sendHttp(server, "POST",
                             QJsonDocument(batch).toJson(QJsonDocument::Compact), sid);
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.jsonArrayBody.size(), 2);
        QCOMPARE(resp.jsonArrayBody[0].toObject()["id"].toInt(), 11);
        QCOMPARE(resp.jsonArrayBody[1].toObject()["id"].toInt(), 12);
        QVERIFY(resp.jsonArrayBody[1].toObject().contains("result"));
    }

    // A notification carries no `id` and so gets no array slot. A batch of
    // nothing but notifications therefore has no response at all, which is 202 —
    // the same answer a single notification gets.
    void batchOfNotificationsOnlyReturns202()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        QJsonArray batch;
        batch.append(QJsonDocument::fromJson(notifyBody("notifications/initialized")).object());
        batch.append(QJsonDocument::fromJson(notifyBody("notifications/initialized")).object());

        auto resp = sendHttp(server, "POST",
                             QJsonDocument(batch).toJson(QJsonDocument::Compact), sid);
        QCOMPARE(resp.statusCode, 202);
        QVERIFY(resp.rawBody.isEmpty());
    }

    void batchMixedRequestAndNotificationAnswersOnlyTheRequest()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        QJsonArray batch;
        batch.append(QJsonDocument::fromJson(notifyBody("notifications/initialized")).object());
        batch.append(QJsonDocument::fromJson(rpcBody("ping", {}, 21)).object());

        auto resp = sendHttp(server, "POST",
                             QJsonDocument(batch).toJson(QJsonDocument::Compact), sid);
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.jsonArrayBody.size(), 1);
        QCOMPARE(resp.jsonArrayBody[0].toObject()["id"].toInt(), 21);
    }

    void batchEmptyArrayIsInvalidRequest()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        auto resp = sendHttp(server, "POST", "[]", sid);
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.jsonBody["error"].toObject()["code"].toInt(), -32600);
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

    // A batched call to a deferring tool must NOT be dispatched. The refusal used
    // to be read off handleJsonRpc's `_deferred` return — i.e. after the tool had
    // already run — so the tool took effect and then wrote a second complete HTTP
    // body onto a socket the batch response had already answered.
    //
    // The stub records whether it ran, which is the assertion that distinguishes
    // "refused" from "refused after doing it anyway".
    void batchedAsyncToolIsRefusedWithoutBeingDispatched()
    {
        McpServer server;
        auto dispatched = std::make_shared<bool>(false);
        server.toolRegistry()->registerAsyncTool(
            "stub_async_side_effect",
            "Records that it ran",
            QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}},
            [dispatched](const QJsonObject&, std::function<void(QJsonObject)> respond) {
                *dispatched = true;
                respond(QJsonObject{{"success", true}});
            },
            "read");

        const QString sid = openSession(server, "2025-11-25");

        QJsonObject callParams;
        callParams["name"] = "stub_async_side_effect";
        callParams["arguments"] = QJsonObject{};

        QJsonArray batch;
        batch.append(QJsonDocument::fromJson(rpcBody("ping", {}, 61)).object());
        batch.append(QJsonDocument::fromJson(rpcBody("tools/call", callParams, 62)).object());

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("Batched tools/call refused before dispatch"));

        auto resp = sendHttp(server, "POST",
                             QJsonDocument(batch).toJson(QJsonDocument::Compact), sid);

        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.jsonArrayBody.size(), 2);
        QVERIFY2(!*dispatched,
                 "a batched async tool must not run — refusing it after it has already "
                 "taken effect is not refusing it");

        // Look the slot up by id rather than by index: JSON-RPC 2.0 lets a server
        // answer a batch in any order, and clients correlate by id.
        QJsonObject refused;
        for (const QJsonValue& v : std::as_const(resp.jsonArrayBody))
            if (v.toObject()["id"].toInt() == 62) refused = v.toObject();
        QVERIFY(!refused.isEmpty());
        QCOMPARE(refused["error"].toObject()["code"].toInt(), -32600);

        // Exactly one HTTP response on the socket — a second body would desync a
        // keep-alive connection rather than merely confuse the parse.
        QVERIFY2(!resp.rawBody.contains("HTTP/1.1"),
                 "a second complete HTTP response was written into the body");
    }

    // JSON-RPC 2.0 §6's "rpc call with invalid Batch": a non-object element gets
    // its own error slot with a null id. Shortening the array instead would
    // desync the client's id correlation silently.
    void batchWithNonObjectElementAnswersItWithNullId()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        QJsonArray batch;
        batch.append(QJsonDocument::fromJson(rpcBody("ping", {}, 71)).object());
        batch.append(QJsonValue(7));

        auto resp = sendHttp(server, "POST",
                             QJsonDocument(batch).toJson(QJsonDocument::Compact), sid);
        QCOMPARE(resp.jsonArrayBody.size(), 2);
        const QJsonObject bad = resp.jsonArrayBody[1].toObject();
        QCOMPARE(bad["error"].toObject()["code"].toInt(), -32600);
        QVERIFY2(bad["id"].isNull(), "an unidentifiable request answers with a null id");
    }

    // The empty-array and parse-error paths must answer with `id: null`, not
    // `id: 0`. QJsonValue::Null is an unscoped enumerator of value 0, so passing
    // it where a QVariant is expected picks QVariant(int) over
    // QVariant(const QJsonValue&) — an integral promotion beats a user-defined
    // conversion — and 0 is a legal id a client may correlate against a real
    // request.
    void unidentifiableRequestsAnswerWithNullId()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");

        auto empty = sendHttp(server, "POST", "[]", sid);
        QVERIFY2(empty.jsonBody["id"].isNull(), "empty batch must answer id: null");

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
        auto resp = sendHttp(server, "POST", rpcBody("tools/call", params, 91), sid);

        QCOMPARE(spy.count(), 1);
        QVERIFY2(!confirmationPayloadText(resp).contains("needs_confirmation"),
                 "the in-app dialog owns this tool; a chat prompt as well is a double prompt");
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
