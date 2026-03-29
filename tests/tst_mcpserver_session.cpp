#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>

#include "mcp/mcpserver.h"
#include "mcp/mcpsession.h"
#include "mcp/mcptoolregistry.h"
#include "mcp/mcpresourceregistry.h"

// Stub register functions — this test doesn't exercise tools/resources,
// only session management, ping, and subscribe. Providing stubs avoids
// linking the entire tool/resource stack and all their transitive dependencies.
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
void registerControlTools(McpToolRegistry*, DE1Device*, MachineState*, ProfileManager*) {}
void registerWriteTools(McpToolRegistry*, ProfileManager*, ShotHistoryStorage*, Settings*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*) {}
void registerScaleTools(McpToolRegistry*, MachineState*) {}
void registerDeviceTools(McpToolRegistry*, BLEManager*, DE1Device*) {}
void registerDebugTools(McpToolRegistry*, MemoryMonitor*) {}
void registerMcpResources(McpResourceRegistry*, DE1Device*, MachineState*, ProfileManager*, ShotHistoryStorage*, MemoryMonitor*) {}

// Test McpServer session management: findOrCreateSession, ping, subscribe/unsubscribe.
// These tests exercise the server directly without full BLE/profile wiring.

class tst_McpServerSession : public QObject {
    Q_OBJECT

private:
    // Helper: send a JSON-RPC request to the server and return the response
    struct RpcResult {
        QJsonObject response;
        QString sessionId;
    };

    static RpcResult sendRpc(McpServer& server, const QString& method,
                             const QJsonObject& params, const QString& sessionId = QString(),
                             int id = 1)
    {
        // Create a local TCP server + connected socket pair for the response
        QTcpServer tcpServer;
        tcpServer.listen(QHostAddress::LocalHost);

        QTcpSocket clientSocket;
        clientSocket.connectToHost(QHostAddress::LocalHost, tcpServer.serverPort());
        QVERIFY2_HELPER(tcpServer.waitForNewConnection(1000), "TCP server accept");
        QTcpSocket* serverSocket = tcpServer.nextPendingConnection();
        QVERIFY2_HELPER(clientSocket.waitForConnected(1000), "TCP client connect");

        // Build request
        QJsonObject request;
        request["jsonrpc"] = "2.0";
        request["id"] = id;
        request["method"] = method;
        request["params"] = params;
        QByteArray body = QJsonDocument(request).toJson(QJsonDocument::Compact);

        // Build headers
        QByteArray headers = "Content-Type: application/json\r\n";
        if (!sessionId.isEmpty())
            headers += "Mcp-Session-Id: " + sessionId.toUtf8() + "\r\n";

        server.handleHttpRequest(serverSocket, "POST", "/mcp", headers, body);

        // Read response from the client side
        clientSocket.waitForReadyRead(1000);
        QByteArray rawResponse = clientSocket.readAll();

        RpcResult result;

        // Extract session ID from response headers
        for (const QByteArray& line : rawResponse.split('\n')) {
            QByteArray lower = line.trimmed().toLower();
            if (lower.startsWith("mcp-session-id:")) {
                result.sessionId = QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
                break;
            }
        }

        // Parse JSON body (after empty line)
        int bodyStart = rawResponse.indexOf("\r\n\r\n");
        if (bodyStart >= 0) {
            QByteArray jsonBody = rawResponse.mid(bodyStart + 4);
            result.response = QJsonDocument::fromJson(jsonBody).object();
        }

        serverSocket->close();
        clientSocket.close();
        return result;
    }

    // QVERIFY can't be used in non-test-slot helpers — use this macro-less wrapper
    static bool QVERIFY2_HELPER(bool cond, const char* msg) {
        if (!cond) qWarning("VERIFY FAILED: %s", msg);
        return cond;
    }

private slots:
    void initTestCase()
    {
        // Suppress expected warnings from McpServer without full wiring
        // (no tools/resources registered, etc.)
    }

    // --- findOrCreateSession ---

    void testInitializeCreatesSession()
    {
        McpServer server;
        auto result = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});

        QVERIFY(!result.sessionId.isEmpty());
        QVERIFY(result.response.contains("result"));
        QVERIFY(result.response["result"].toObject().contains("protocolVersion"));
        QCOMPARE(server.activeSessionCount(), 1);
    }

    void testReinitializeReusesSameSession()
    {
        McpServer server;

        // First initialize — creates session
        auto first = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});
        QVERIFY(!first.sessionId.isEmpty());
        QCOMPARE(server.activeSessionCount(), 1);

        // Re-initialize with same session ID — should reuse, not create new
        auto second = sendRpc(server, "initialize",
                              QJsonObject{{"capabilities", QJsonObject{}}},
                              first.sessionId);
        QCOMPARE(second.sessionId, first.sessionId);
        QCOMPARE(server.activeSessionCount(), 1);  // still 1, no leak
    }

    void testReinitializeWithStaleIdCreatesNew()
    {
        McpServer server;

        // First initialize
        auto first = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});
        QCOMPARE(server.activeSessionCount(), 1);

        // Re-initialize with non-existent session ID — should create new
        auto second = sendRpc(server, "initialize",
                              QJsonObject{{"capabilities", QJsonObject{}}},
                              "00000000-0000-0000-0000-000000000000");
        QVERIFY(!second.sessionId.isEmpty());
        QVERIFY(second.sessionId != "00000000-0000-0000-0000-000000000000");
        QCOMPARE(server.activeSessionCount(), 2);  // original + new
    }

    void testInitializeWithoutHeaderCreatesNew()
    {
        McpServer server;

        auto first = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});
        QCOMPARE(server.activeSessionCount(), 1);

        // Initialize without session header — should always create new
        auto second = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});
        QVERIFY(!second.sessionId.isEmpty());
        QVERIFY(second.sessionId != first.sessionId);
        QCOMPARE(server.activeSessionCount(), 2);
    }

    void testMultipleReconnectsDoNotLeak()
    {
        McpServer server;

        // First connect
        auto initial = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});
        QString sid = initial.sessionId;
        QCOMPARE(server.activeSessionCount(), 1);

        // Simulate 5 reconnects with same session ID
        for (int i = 0; i < 5; i++) {
            auto reconnect = sendRpc(server, "initialize",
                                     QJsonObject{{"capabilities", QJsonObject{}}}, sid);
            QCOMPARE(reconnect.sessionId, sid);
        }

        // Should still be exactly 1 session
        QCOMPARE(server.activeSessionCount(), 1);
    }

    // --- ping ---

    void testPingReturnsEmptyResult()
    {
        McpServer server;
        auto init = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});

        auto ping = sendRpc(server, "ping", QJsonObject{}, init.sessionId, 2);
        QVERIFY(ping.response.contains("result"));
        QJsonObject result = ping.response["result"].toObject();
        QVERIFY(result.isEmpty());  // empty object per spec
        QVERIFY(!ping.response.contains("error"));
    }

    // --- resources/subscribe ---

    void testResourcesSubscribe()
    {
        McpServer server;
        auto init = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});

        QJsonObject subParams;
        subParams["uri"] = "decenza://machine/state";
        auto sub = sendRpc(server, "resources/subscribe", subParams, init.sessionId, 2);

        QVERIFY(sub.response.contains("result"));
        QVERIFY(!sub.response.contains("error"));
    }

    void testResourcesSubscribeMissingUri()
    {
        McpServer server;
        auto init = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});

        auto sub = sendRpc(server, "resources/subscribe", QJsonObject{}, init.sessionId, 2);

        // Should return an error about missing uri
        QJsonObject result = sub.response["result"].toObject();
        QVERIFY(result.contains("error") || sub.response.contains("error"));
    }

    void testResourcesUnsubscribe()
    {
        McpServer server;
        auto init = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});

        // Subscribe then unsubscribe
        QJsonObject params;
        params["uri"] = "decenza://machine/state";
        sendRpc(server, "resources/subscribe", params, init.sessionId, 2);

        auto unsub = sendRpc(server, "resources/unsubscribe", params, init.sessionId, 3);
        QVERIFY(unsub.response.contains("result"));
        QVERIFY(!unsub.response.contains("error"));
    }

    // --- DELETE session ---

    void testDeleteSessionThenReinitialize()
    {
        McpServer server;

        auto init = sendRpc(server, "initialize", QJsonObject{{"capabilities", QJsonObject{}}});
        QString sid = init.sessionId;
        QCOMPARE(server.activeSessionCount(), 1);

        // Delete the session via HTTP DELETE
        QTcpServer tcpServer;
        tcpServer.listen(QHostAddress::LocalHost);
        QTcpSocket clientSocket;
        clientSocket.connectToHost(QHostAddress::LocalHost, tcpServer.serverPort());
        tcpServer.waitForNewConnection(1000);
        QTcpSocket* serverSocket = tcpServer.nextPendingConnection();
        clientSocket.waitForConnected(1000);

        QByteArray headers = "Mcp-Session-Id: " + sid.toUtf8() + "\r\n";
        server.handleHttpRequest(serverSocket, "DELETE", "/mcp", headers, QByteArray());

        serverSocket->close();
        clientSocket.close();

        QCOMPARE(server.activeSessionCount(), 0);

        // Re-initialize with deleted session ID — should create new
        auto reinit = sendRpc(server, "initialize",
                              QJsonObject{{"capabilities", QJsonObject{}}}, sid);
        QVERIFY(!reinit.sessionId.isEmpty());
        QVERIFY(reinit.sessionId != sid);  // new ID, old one was deleted
        QCOMPARE(server.activeSessionCount(), 1);
    }

    // --- Auto-recover for non-initialize requests ---

    void testAutoRecoverCreatesNewSession()
    {
        McpServer server;

        // Send a non-initialize request with unknown session — should auto-recover
        auto result = sendRpc(server, "tools/list", QJsonObject{},
                              "nonexistent-session-id", 2);

        // Should succeed (auto-recovered) or have tools list
        // The response should have a new session ID
        QVERIFY(!result.sessionId.isEmpty());
        QVERIFY(result.sessionId != "nonexistent-session-id");
        QCOMPARE(server.activeSessionCount(), 1);
    }
};

QTEST_MAIN(tst_McpServerSession)
#include "tst_mcpserver_session.moc"
