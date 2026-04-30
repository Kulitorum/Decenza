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
#include <QTcpServer>
#include <QTcpSocket>
#include <QPair>

#include "mcp/mcpserver.h"
#include "mcp/mcpsession.h"
#include "mcp/mcptoolregistry.h"
#include "mcp/mcpresourceregistry.h"

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
class TranslationManager;
class BatteryManager;
class BLEManager;
class MemoryMonitor;
void registerMachineTools(McpToolRegistry*, DE1Device*, MachineState*, MainController*, ProfileManager*) {}
void registerShotTools(McpToolRegistry*, ShotHistoryStorage*) {}
void registerProfileTools(McpToolRegistry*, ProfileManager*) {}
void registerSettingsReadTools(McpToolRegistry*, Settings*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*) {}
void registerDialingTools(McpToolRegistry*, MainController*, ProfileManager*, ShotHistoryStorage*, Settings*) {}
void registerControlTools(McpToolRegistry*, DE1Device*, MachineState*, ProfileManager*, MainController*, Settings*) {}
void registerWriteTools(McpToolRegistry*, ProfileManager*, ShotHistoryStorage*, Settings*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*) {}
void registerScaleTools(McpToolRegistry*, MachineState*) {}
void registerDeviceTools(McpToolRegistry*, BLEManager*, DE1Device*) {}
void registerDebugTools(McpToolRegistry*, MemoryMonitor*) {}
void registerMcpResources(McpResourceRegistry*, DE1Device*, MachineState*, ProfileManager*, ShotHistoryStorage*, MemoryMonitor*, Settings*) {}
void registerAgentTools(McpToolRegistry*) {}

class tst_McpServerProtocol : public QObject {
    Q_OBJECT

private:
    struct HttpResponse {
        int statusCode = 0;
        QJsonObject jsonBody;        // empty when body isn't JSON or is missing
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

        const int firstLineEnd = raw.indexOf("\r\n");
        if (firstLineEnd > 0) {
            const QByteArray statusLine = raw.left(firstLineEnd);
            const auto parts = statusLine.split(' ');
            if (parts.size() >= 2) out.statusCode = parts[1].toInt();
        }

        for (const QByteArray& line : raw.split('\n')) {
            const QByteArray lower = line.trimmed().toLower();
            const auto extract = [&](const QByteArray& want) {
                return QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
            };
            if (lower.startsWith("mcp-session-id:"))
                out.sessionId = extract("mcp-session-id");
            else if (lower.startsWith("mcp-protocol-version:"))
                out.protocolVersion = extract("mcp-protocol-version");
            else if (lower.startsWith("access-control-allow-origin:"))
                out.allowOrigin = extract("access-control-allow-origin");
        }

        const qsizetype bodyStart = raw.indexOf("\r\n\r\n");
        if (bodyStart >= 0) {
            const QByteArray bodyBytes = raw.mid(bodyStart + 4);
            out.rawBody = QString::fromUtf8(bodyBytes);
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(bodyBytes, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                out.jsonBody = doc.object();
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

    // ─── MCP-Protocol-Version request header validation ────────────────────

    void protocolVersionHeaderMismatchReturns400()
    {
        McpServer server;
        const QString sid = openSession(server, "2025-11-25");
        QVERIFY(!sid.isEmpty());

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
        auto resp = sendHttp(server, "POST", "{not valid json}", {},
                             {{"Origin", "http://evil.example"}});
        QCOMPARE(resp.statusCode, 403);
    }

    // ─── tools/list shape (title, icons, JSON Schema dialect) ──────────────

    void toolsListIncludesTitleAndIcons()
    {
        McpServer server;
        // Register one inline tool so tools/list has something to inspect.
        server.toolRegistry()->registerTool(
            "shots_get_detail",  // Use a real prefix so icon mapping resolves.
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

        // 2025-11-25: icons as data URIs.
        const QJsonArray icons = t["icons"].toArray();
        QVERIFY2(!icons.isEmpty(), "tools/list entries must include at least one icon");
        const QJsonObject icon = icons[0].toObject();
        QCOMPARE(icon["mimeType"].toString(), QString("image/svg+xml"));
        QVERIFY(icon["src"].toString().startsWith("data:image/svg+xml;base64,"));

        // 2025-11-25: JSON Schema 2020-12 dialect declared.
        QCOMPARE(t["inputSchema"].toObject()["$schema"].toString(),
                 QString("https://json-schema.org/draft/2020-12/schema"));
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

        // structuredContent must NOT carry the side-channel field — it's
        // consumed by the wrapper and stripped from the structured payload.
        const QJsonObject structured = result["structuredContent"].toObject();
        QVERIFY2(!structured.contains("_resourceLinks"),
                 "buildToolCallResponse must strip _resourceLinks from structuredContent");
        QVERIFY(structured.contains("items"));
    }

    // ─── Pure helpers ──────────────────────────────────────────────────────

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
