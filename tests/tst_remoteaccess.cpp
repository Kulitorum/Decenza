// Tests for the remote MCP connector (RemoteAccess) added by the
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
// Drives a real RemoteAccess listener over a loopback TCP socket. The
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
#include <QTcpServer>
#include <QPointer>
#ifdef Q_OS_UNIX
#include <poll.h>
#include <sys/ioctl.h>
#endif

#include "network/remoteaccess.h"
#include "mcp/mcpserver.h"
#include "mcp/mcptoolregistry.h"
#include "mcp/mcpresourceregistry.h"
#include "core/settings_mcp.h"
#include "core/settings_network.h"
#include "core/capabilitytoken.h"
#include "network/remotewebroute.h"
#include <QNetworkInterface>
#include <QUuid>

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
void registerSettingsReadTools(McpToolRegistry*, Settings*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*, AIManager*, MainController*) {}
void registerDialingTools(McpToolRegistry*, MainController*, ProfileManager*, ShotHistoryStorage*, Settings*) {}
void registerControlTools(McpToolRegistry*, DE1Device*, MachineState*, ProfileManager*, MainController*, Settings*) {}
void registerWriteTools(McpToolRegistry*, ProfileManager*, ShotHistoryStorage*, Settings*, CoffeeBagStorage*, AccessibilityManager*, ScreensaverVideoManager*, TranslationManager*, BatteryManager*, AIManager*, BeanBaseClient*, MainController*) {}
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

class tst_RemoteAccess : public QObject {
    Q_OBJECT

    // Parsed HTTP response from the loopback listener.
    // A failed status says whether the connect or the response never arrived.
#define QCOMPARE_STATUS(respExpr, expected) \
    do { \
        const Resp r_ = (respExpr); \
        QVERIFY2(r_.status == (expected), qPrintable(QStringLiteral("status %1, expected %2: %3") \
                     .arg(r_.status).arg(expected).arg(r_.failure))); \
    } while (0)

    struct Resp {
        int status = 0;
        QString failure;   // why status is 0, for a failure message
        QByteArray headers;
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
        // A refused connect returns to Unconnected; waiting out the budget would only hide it.
        while (sock.state() != QAbstractSocket::ConnectedState
               && sock.state() != QAbstractSocket::UnconnectedState && ct.elapsed() < 3000)
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
            r.headers = buf.left(headerEnd);
            r.rawBody = buf.mid(headerEnd + 4);
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(r.rawBody, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                r.json = doc.object();
        }
        return r;
    }

    // The listener startRemote() last stood up, for describing a stalled request.
    // A QPointer because each test function's instance dies with that function.
    static inline QPointer<RemoteAccess> s_listenerUnderTest;

    // Where a request with no answer stopped. Connected-with-no-reply has three
    // causes that look identical from the client: the connection is still in
    // the kernel's accept queue (Qt never accepted it, or the QTcpServer paused
    // itself after an accept error, which nothing in RemoteAccess handles),
    // it was accepted but its bytes were never read, or the reply is sitting
    // unread in the client's kernel buffer. Those are three different bugs.
    static QString describeStall(const QTcpSocket& client)
    {
        QString out;
#ifdef Q_OS_UNIX
        int clientUnread = -1;
        if (client.socketDescriptor() >= 0)
            ::ioctl(int(client.socketDescriptor()), FIONREAD, &clientUnread);
        out += QStringLiteral("; client kernel has %1 unread bytes").arg(clientUnread);
#endif
        const RemoteAccess* remote = s_listenerUnderTest.data();
        if (!remote || !remote->m_listener)
            return out + QStringLiteral("; listener state unknown");
        const QTcpServer* listener = remote->m_listener;
        out += QStringLiteral("; listener listening=%1 serverError=%2 (%3)")
                   .arg(listener->isListening()).arg(int(listener->serverError()))
                   .arg(listener->errorString());
#ifdef Q_OS_UNIX
        pollfd pfd{int(listener->socketDescriptor()), POLLIN, 0};
        if (pfd.fd >= 0 && ::poll(&pfd, 1, 0) >= 0)
            out += QStringLiteral(", connection waiting in accept queue=%1")
                       .arg((pfd.revents & POLLIN) != 0);
#endif
        qsizetype buffered = 0;
        for (const auto& pending : remote->m_pending)
            buffered += pending.buffer.size();
        return out + QStringLiteral(", accepted sockets open=%1, server bytes buffered=%2")
                         .arg(remote->m_sockets.size()).arg(buffered);
    }

    static Resp fetch(quint16 port, const QByteArray& request, QTcpSocket* reuse = nullptr)
    {
        QTcpSocket local;
        QTcpSocket* sock = reuse ? reuse : &local;
        QElapsedTimer t;
        t.start();
        const auto describe = [&](const char* what) {
            return QStringLiteral("%1 after %2 ms, socket state %3, error %4 (%5)%6")
                .arg(QLatin1String(what)).arg(t.elapsed()).arg(int(sock->state()))
                .arg(int(sock->error())).arg(sock->errorString(), describeStall(*sock));
        };
        if (sock->state() != QAbstractSocket::ConnectedState && !pumpConnected(*sock, port)) {
            Resp r;
            r.failure = describe("connect failed");
            return r;
        }
        sock->write(request);
        sock->flush();
        Resp r = readResponse(*sock);
        if (r.status == 0)
            r.failure = describe("no response");
        return r;
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

    // Stand up McpServer + RemoteAccess bound to an ephemeral port. Registers
    // a read-level and a control-level tool for the integration/access tests.
    // Returns the listening port; token is read from `settings`.
    static quint16 startRemote(SettingsMcp& settings, McpServer& server, RemoteAccess& remote,
                               SettingsNetwork& network)
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
        remote.setNetworkSettings(&network);
        remote.refresh();
        s_listenerUnderTest = &remote;
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
        "mcp/remotePort", "mcp/remoteCustomBaseUrl", "mcp/remoteToken",
        "shotServer/enabled", "shotServer/remoteWebEnabled", "shotServer/remoteWebToken"};

    // Stands in for ShotServer: records admissions and hand-offs.
    class FakeWebHost : public RemoteWebHost {
    public:
        bool serving = true;
        QSet<QString> admissions;
        QList<QPointer<QTcpSocket>> adopted;
        QByteArray firstAdoptedPeek;
        int ended = 0;
        QObject owner;   // parents adopted sockets, as ShotServer does

        bool remoteWebServing() const override { return serving; }
        QString createRemoteWebAdmission(const QString&) override
        {
            const QString id = QUuid::createUuid().toString(QUuid::Id128);
            admissions.insert(id);
            return id;
        }
        bool remoteWebAdmissionValid(const QString& id) const override { return admissions.contains(id); }
        void adoptRemoteWebConnection(QTcpSocket* socket) override
        {
            if (adopted.isEmpty())
                firstAdoptedPeek = socket->peek(4096);
            socket->setParent(&owner);
            adopted.append(socket);
        }
        void endRemoteWebSessions() override
        {
            ++ended;
            admissions.clear();
            for (const auto& socket : std::as_const(adopted))
                if (socket)
                    socket->close();
        }
    };

    // A tunnel-proxied listener with the web route on (and the MCP route when
    // asked). Every setting is written before the coordinator is wired to them,
    // and the listener is started directly rather than through refresh(), so it
    // holds in builds compiled without tsnet and no later rebind moves the port.
    static quint16 startWebRoute(SettingsMcp& mcp, SettingsNetwork& network, RemoteAccess& remote,
                                 FakeWebHost& host, bool withMcp = false)
    {
        mcp.setMcpEnabled(withMcp);
        mcp.setRemoteMcpEnabled(withMcp);
        mcp.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        mcp.setRemoteMcpPort(0);   // ephemeral
        network.setShotServerEnabled(true);
        network.setRemoteWebEnabled(true);
        remote.setSettings(&mcp);
        remote.setNetworkSettings(&network);
        remote.setWebHost(&host);
        remote.m_webWasWanted = true;   // what refresh() would have recorded
        remote.m_mcpWasWanted = withMcp;
        remote.startListener(/*bindLoopbackOnly=*/true);
        s_listenerUnderTest = &remote;
        return static_cast<quint16>(remote.listenPort());
    }

    static QByteArray webGet(const QByteArray& path, const QByteArray& cookie = {},
                             const QByteArray& method = "GET")
    {
        QByteArray req = method + " " + path + " HTTP/1.1\r\nHost: decenza.example.ts.net\r\n";
        if (!cookie.isEmpty())
            req += "Cookie: other=1; " + QByteArray(RemoteWebRoute::kAdmissionCookie) + "=" + cookie + "\r\n";
        return req + "\r\n";
    }

    // The admission id from a 303's Set-Cookie, or empty.
    static QByteArray admittedCookie(const Resp& r)
    {
        const QByteArray name = QByteArray("Set-Cookie: ") + RemoteWebRoute::kAdmissionCookie + "=";
        const qsizetype at = r.headers.indexOf(name);
        if (at < 0)
            return {};
        const qsizetype start = at + name.size();
        return r.headers.mid(start, r.headers.indexOf(';', start) - start);
    }

    // Send one request and pump until the host adopts the connection (or not).
    static bool sentAndAdopted(quint16 port, const QByteArray& request, FakeWebHost& host,
                               QTcpSocket& client)
    {
        if (!pumpConnected(client, port))
            return false;
        const qsizetype before = host.adopted.size();
        client.write(request);
        client.flush();
        QElapsedTimer t;
        t.start();
        while (host.adopted.size() == before && t.elapsed() < 1500
               && client.state() == QAbstractSocket::ConnectedState)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        return host.adopted.size() > before;
    }
    QHash<QString, QVariant> m_savedSettings;
    // Enable Network gates the MCP route too; every test starts with it on and
    // the web route off.
    SettingsNetwork m_network;

private slots:
    void init()
    {
        QTest::failOnWarning();
        m_network.setShotServerEnabled(true);
        m_network.setRemoteWebEnabled(false);
    }
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

    // ── Funnel probe failures (friend access) ─────────────────────────────
    // A device that has Funnel, while Tailscale is still bringing its URL up, used
    // to be told after 30 s to go and enable Funnel.
    void classifyProbeFailure_data()
    {
        using PF = RemoteAccess::ProbeFailure;
        QTest::addColumn<int>("grant");
        QTest::addColumn<bool>("hostNotFound");
        QTest::addColumn<int>("failCount");
        QTest::addColumn<int>("expected");
        const int granted = McpTunnelTsnet::Granted;
        const int notGranted = McpTunnelTsnet::NotGranted;
        const int unknown = McpTunnelTsnet::GrantUnknown;
        const int dnsLimit = RemoteAccess::kProbeFailuresBeforeDnsError;
        const int answerLimit = RemoteAccess::kProbeFailuresBeforeNotAnsweringError;
        const int grace = RemoteAccess::kProbeFailuresBeforeError;
        QTest::newRow("granted, unpublished: waiting at once") << granted << true << 1 << int(PF::WaitingForTailscale);
        QTest::newRow("granted, unpublished: still waiting") << granted << true << dnsLimit - 1 << int(PF::WaitingForTailscale);
        QTest::newRow("granted, never published") << granted << true << dnsLimit << int(PF::DnsNotPublished);
        // The restart warm-up: the name resolves and the connection is closed for ~40 s.
        QTest::newRow("granted, warming up: past the grace") << granted << false << grace << int(PF::WaitingForTailscale);
        QTest::newRow("granted, warming up: still waiting") << granted << false << answerLimit - 1 << int(PF::WaitingForTailscale);
        QTest::newRow("granted, never answers") << granted << false << answerLimit << int(PF::NotAnswering);
        QTest::newRow("not granted: grace") << notGranted << true << grace - 1 << int(PF::Verifying);
        QTest::newRow("not granted") << notGranted << true << grace << int(PF::FunnelNotGranted);
        QTest::newRow("grant unknown, unpublished") << unknown << true << grace << int(PF::Unreachable);
    }

    void classifyProbeFailure()
    {
        QFETCH(int, grant);
        QFETCH(bool, hostNotFound);
        QFETCH(int, failCount);
        QFETCH(int, expected);
        QCOMPARE(int(RemoteAccess::classifyProbeFailure(
                     static_cast<McpTunnelTsnet::FunnelGrant>(grant), hostNotFound, failCount)),
                 expected);
    }

    // The setup button used to show whenever the status wasn't Active, so a
    // configured device offered setup on every start and every transient error.
    void setupNeeded_data()
    {
        using T = McpTunnelTsnet;
        using R = RemoteAccess;
        QTest::addColumn<int>("grant");
        QTest::addColumn<int>("tunnelState");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("expected");
        QTest::newRow("granted, waiting for Tailscale") << int(T::Granted) << int(T::Running) << int(R::Publishing) << false;
        QTest::newRow("granted, transient error") << int(T::Granted) << int(T::Running) << int(R::Error) << false;
        QTest::newRow("not granted") << int(T::NotGranted) << int(T::Running) << int(R::Error) << true;
        QTest::newRow("HTTPS certificates off") << int(T::HttpsOff) << int(T::Starting) << int(R::Error) << true;
        QTest::newRow("starting, nothing known yet") << int(T::GrantUnknown) << int(T::Starting) << int(R::Starting) << false;
        QTest::newRow("needs sign-in") << int(T::GrantUnknown) << int(T::NeedsLogin) << int(R::Starting) << true;
        QTest::newRow("tunnel failed") << int(T::GrantUnknown) << int(T::Error) << int(R::Error) << true;
    }

    void setupNeeded()
    {
        QFETCH(int, grant);
        QFETCH(int, tunnelState);
        QFETCH(int, status);
        QFETCH(bool, expected);
        QCOMPARE(RemoteAccess::setupNeeded(static_cast<McpTunnelTsnet::FunnelGrant>(grant),
                                              static_cast<McpTunnelTsnet::State>(tunnelState),
                                              static_cast<RemoteAccess::Status>(status)),
                 expected);
    }

    // ── Constant-time comparison (friend access) ──────────────────────────
    void constantTimeCompare()
    {
        SettingsMcp settings;
        settings.rotateRemoteMcpToken();
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        QVERIFY(CapabilityToken::matches(token, token));
        QVERIFY(!CapabilityToken::matches(QByteArray(), token));
        QVERIFY(!CapabilityToken::matches("short", token));
        QVERIFY(!CapabilityToken::matches(QByteArray(), QByteArray()));
        // Same length, one byte off.
        QByteArray wrong = token;
        wrong[0] = wrong[0] == 'A' ? 'B' : 'A';
        QVERIFY(!CapabilityToken::matches(wrong, token));
    }

    // ── Failed-token rate limiting (friend access) ────────────────────────
    void rateLimit()
    {
        RemoteAccess remote;
        // Under the per-minute budget: not limited.
        for (int i = 0; i < RemoteAccess::MaxFailedPerMinute; ++i)
            QVERIFY(!remote.failedTokenOverLimit("1.2.3.4"));
        // The next failure in the window trips the limit.
        QVERIFY(remote.failedTokenOverLimit("1.2.3.4"));
        // A different source has its own budget.
        QVERIFY(!remote.failedTokenOverLimit("5.6.7.8"));

        // The budget is what bounds how much log an unauthenticated caller can
        // write, so it is pinned at a value small enough to matter. Twenty (the
        // value this replaced) let one source emit twenty warnings a minute,
        // which is enough to evict every other subsystem from the debug buffer.
        QVERIFY(RemoteAccess::MaxFailedPerMinute <= 5);
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
        RemoteAccess remote;
        settings.setMcpEnabled(true);
        settings.setRemoteMcpEnabled(true);   // a token opens its route only while it is on
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.rotateRemoteMcpToken();
        remote.setMcpServer(&server);
        remote.setSettings(&settings);
        remote.setNetworkSettings(&m_network);
        remote.startListener(/*bindLoopbackOnly=*/true);
        s_listenerUnderTest = &remote;
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
        QCOMPARE_STATUS(fetch(port, malformedFromHolder), 404);

        // A valid token still works through the same listener — the silence is
        // for failed authorization only, not for the route itself.
        Resp ok = fetch(port, httpRequest("POST", "/mcp/" + settings.remoteMcpToken().toUtf8(),
                                          rpc("initialize", initParams())));
        QCOMPARE_STATUS(ok, 200);
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
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
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
        QCOMPARE(s_unauthorizedWarnings, RemoteAccess::MaxFailedPerMinute + 2);
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
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // Non-MCP path → bare 404 (unauthorized warning is expected).
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        Resp favicon = fetch(port, httpRequest("GET", "/favicon.ico", {}));
        QCOMPARE_STATUS(favicon, 404);
        QVERIFY(favicon.rawBody.isEmpty());

        // Wrong token → bare 404.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        Resp wrong = fetch(port, httpRequest("POST", "/mcp/not-the-real-token",
                                             rpc("initialize")));
        QCOMPARE_STATUS(wrong, 404);

        // Valid token, initialize → dispatched to McpServer (200 JSON-RPC).
        const QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1.0"}}}};
        Resp ok = fetch(port, httpRequest("POST", "/mcp/" + token, rpc("initialize", params)));
        QCOMPARE_STATUS(ok, 200);
        QVERIFY(ok.json.contains("result"));
        QVERIFY(!ok.sessionId.isEmpty());
    }

    // ── An accept() failure must not wedge the listener ───────────────────
    // QTcpServer answers any accept() failure but EAGAIN by pausing itself while
    // still reporting isListening(), so the reaper's rebind never fired and
    // every later client connected and hung. Driven by what Qt does on that
    // failure (qtcpserver.cpp:188-191), since a real EMFILE cannot be arranged.
    void acceptErrorDoesNotWedgeTheListener()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        QVERIFY(startRemote(settings, server, remote, m_network) != 0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("listener stopped accepting"));
        remote.m_listener->pauseAccepting();
        emit remote.m_listener->acceptError(QAbstractSocket::SocketResourceError);
        remote.onReaperTick();

        const quint16 port = static_cast<quint16>(remote.listenPort());
        QVERIFY(port != 0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected unauthorized request"));
        QCOMPARE_STATUS(fetch(port, httpRequest("GET", "/favicon.ico", {})), 404);
        QCOMPARE(remote.statusString(), QStringLiteral("active"));
    }

    // ── End-to-end: initialize → tools/call through the listener ──────────
    void integrationInitializeListToolsCall()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();
        const QByteArray path = "/mcp/" + token;

        const QByteArray sid = openSession(port, token);
        QVERIFY(!sid.isEmpty());

        // tools/list surfaces the registered read tool.
        Resp list = fetch(port, httpRequest("POST", path, rpc("tools/list", {}, 2), sid));
        QCOMPARE_STATUS(list, 200);
        const QJsonArray tools = list.json["result"].toObject()["tools"].toArray();
        QVERIFY(tools.size() >= 1);

        // tools/call on the read tool succeeds (default access level = Monitor).
        Resp call = fetch(port, httpRequest("POST", path,
            rpc("tools/call", QJsonObject{{"name", "shots_get_detail"},
                                          {"arguments", QJsonObject{}}}, 3), sid));
        QCOMPARE_STATUS(call, 200);
        const QJsonObject result = call.json["result"].toObject();
        QVERIFY(!result.isEmpty());
        QVERIFY(!result.contains("error"));
    }

    // ── Remote sessions honor access level identically to LAN ─────────────
    void remoteHonorsAccessLevel()
    {
        SettingsMcp settings;
        McpServer server;   // no Settings wired → access level defaults to Monitor(0)
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
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
        QCOMPARE_STATUS(call, 200);
        QVERIFY2(call.json.contains("error"), call.rawBody.constData());
        QVERIFY(call.json["error"].toObject()["message"].toString().contains("Access level"));
    }

    // ── Rotation revokes the old URL and drops live connections ───────────
    void rotationClosesSockets()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray oldToken = settings.remoteMcpToken().toUtf8();

        // Open a live connection and complete a request on the old token.
        QTcpSocket live;
        Resp init = fetch(port, httpRequest("POST", "/mcp/" + oldToken, rpc("initialize")), &live);
        QCOMPARE_STATUS(init, 200);
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
        QCOMPARE_STATUS(stale, 404);

        // The new token works.
        const QByteArray newToken = settings.remoteMcpToken().toUtf8();
        QVERIFY(newToken != oldToken);
        const QJsonObject params{
            {"protocolVersion", "2025-11-25"},
            {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "tst"}, {"version", "1.0"}}}};
        Resp fresh = fetch(port, httpRequest("POST", "/mcp/" + newToken, rpc("initialize", params)));
        QCOMPARE_STATUS(fresh, 200);
    }

    // ── connectorUrl composition (Mode C) ─────────────────────────────────
    void connectorUrlComposition()
    {
        SettingsMcp settings;
        RemoteAccess remote;
        remote.setSettings(&settings);
        remote.setNetworkSettings(&m_network);
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.setMcpEnabled(true);
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpEnabled(true);
        remote.refresh();   // the stored values may already match, so no setter fired it
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

    void urlRevisionMovesOnlyWhenAUrlChanges()
    {
        SettingsMcp settings;
        RemoteAccess remote;
        remote.setSettings(&settings);
        remote.setNetworkSettings(&m_network);
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("https://decenza.example.ts.net"));
        settings.setRemoteMcpEnabled(true);
        remote.refresh();   // the stored values may already match, so no setter fired it
        const int live = remote.urlRevision();

        remote.refresh();   // re-emits connectorUrlChanged with the same URL
        QCOMPARE(remote.urlRevision(), live);
        remote.rotateToken();
        QCOMPARE(remote.urlRevision(), live + 1);
    }

    void customConnectorUrlNeedsAServingListener()
    {
        SettingsMcp settings;
        RemoteAccess remote;
        remote.setSettings(&settings);
        remote.setNetworkSettings(&m_network);
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("https://decenza.example.ts.net"));
        settings.setRemoteMcpEnabled(true);
        remote.refresh();   // the stored values may already match, so no setter fired it
        QVERIFY(!remote.connectorUrl().isEmpty());

        remote.m_listener->close();   // what a failed bind or accept error leaves
        QVERIFY(remote.connectorUrl().isEmpty());
    }

    // ── connectorUrl needs Remote Access and the network, not the local MCP switch ─
    void connectorUrlRequiresRemoteAccessAndNetwork()
    {
        SettingsMcp settings;
        RemoteAccess remote;
        remote.setSettings(&settings);
        remote.setNetworkSettings(&m_network);
        settings.setRemoteMcpPort(0);   // ephemeral
        settings.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        settings.setRemoteMcpEnabled(true);
        settings.setRemoteMcpCustomBaseUrl(QStringLiteral("https://decenza.example.ts.net"));
        remote.refresh();   // the stored values may already match, so no setter fired it

        // "Enable MCP Server" opens only the local endpoint; Remote Access carries MCP itself.
        settings.setMcpEnabled(false);
        QVERIFY(!remote.connectorUrl().isEmpty());
        settings.setRemoteMcpEnabled(false);
        QVERIFY(remote.connectorUrl().isEmpty());   // Remote Access off → no live URL
        settings.setRemoteMcpEnabled(true);
        QVERIFY(!remote.connectorUrl().isEmpty());

        m_network.setShotServerEnabled(false);
        QVERIFY(remote.connectorUrl().isEmpty());   // Enable Network off → no remote MCP
        m_network.setShotServerEnabled(true);
        QVERIFY(!remote.connectorUrl().isEmpty());
    }

    // ── Content-Length validation (body cap + malformed) ──────────────────
    void contentLengthValidation()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // Content-Length beyond the 1 MB body cap → bare 404, connection closed.
        const QByteArray oversized = "POST /mcp/" + token +
            " HTTP/1.1\r\nHost: x\r\nContent-Length: 5000000\r\n\r\n";
        QCOMPARE_STATUS(fetch(port, oversized), 404);

        // Non-numeric Content-Length must be rejected, not silently parsed as 0
        // (which would desync keep-alive framing).
        const QByteArray malformed = "POST /mcp/" + token +
            " HTTP/1.1\r\nHost: x\r\nContent-Length: notanumber\r\n\r\n";
        QCOMPARE_STATUS(fetch(port, malformed), 404);
    }

    // ── Path boundary: trailing segment rejected, query string stripped ───
    void pathBoundary()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        // A trailing segment after the token is not the exact route → 404.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression("rejected unauthorized request"));
        QCOMPARE_STATUS(fetch(port, httpRequest("POST", "/mcp/" + token + "/extra",
                                         rpc("initialize", initParams()))), 404);

        // A query string after the token is stripped → authorized.
        Resp ok = fetch(port, httpRequest("POST", "/mcp/" + token + "?src=claude",
                                          rpc("initialize", initParams())));
        QCOMPARE_STATUS(ok, 200);
        QVERIFY(ok.json.contains("result"));
    }

    // ── Partial-read reassembly (request split across TCP segments) ───────
    void partialRequestReassembly()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray token = settings.remoteMcpToken().toUtf8();

        const QByteArray full = httpRequest("POST", "/mcp/" + token,
                                            rpc("initialize", initParams()));
        // Split mid-request so the server must buffer across reads before it can
        // find the header terminator / complete the body.
        const int mid = static_cast<int>(full.size() / 2);
        Resp r = fetchChunked(port, {full.left(mid), full.mid(mid)});
        QCOMPARE_STATUS(r, 200);
        QVERIFY(r.json.contains("result"));
    }

    // ── Keep-alive pipelining: two requests in one write, both processed ──
    void pipelinedRequests()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        const QByteArray path = "/mcp/" + settings.remoteMcpToken().toUtf8();

        // Two initialize requests concatenated into one write on one socket: the
        // drain loop must process both, creating two sessions.
        const QByteArray two = httpRequest("POST", path, rpc("initialize", initParams(), 1))
                             + httpRequest("POST", path, rpc("initialize", initParams(), 2));
        fetch(port, two);
        QTRY_COMPARE(server.activeSessionCount(), 2);
    }


    // ── Remote Web Access ─────────────────────────────────────────────────
    void webTokenIsItsOwn()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        network.rotateRemoteWebToken();
        const QString token = network.remoteWebToken();
        QCOMPARE(token.size(), 22);
        QCOMPARE(network.remoteWebToken(), token);   // stable across reads
        QVERIFY(token != mcp.remoteMcpToken());
        network.rotateRemoteWebToken();
        QVERIFY(network.remoteWebToken() != token);
    }

    void routesSwitchIndependently()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        mcp.setMcpEnabled(true);
        mcp.setRemoteMcpEnabled(false);
        mcp.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeCustom));
        network.setShotServerEnabled(true);
        network.setRemoteWebEnabled(true);
        remote.m_settings = &mcp;
        remote.m_networkSettings = &network;

        // Web alone: the node, a loopback listener, no MCP route.
        QVERIFY(remote.webRouteWanted());
        QVERIFY(!remote.mcpRouteWanted());
        QVERIFY(remote.tunnelWanted());
        QVERIFY(!remote.lanBindWanted());

        // A Custom-URL connector beside it needs the LAN bind as well.
        mcp.setRemoteMcpEnabled(true);
        QVERIFY(remote.tunnelWanted());
        QVERIFY(remote.lanBindWanted());

        // A Tailscale connector alone still runs the node.
        network.setRemoteWebEnabled(false);
        mcp.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeTailscale));
        QVERIFY(remote.tunnelWanted());
        QVERIFY(!remote.webRouteWanted());

        // Both routes need the network enabled: Enable Network is the master switch.
        network.setRemoteWebEnabled(true);
        network.setShotServerEnabled(false);
        QVERIFY(!remote.webRouteWanted());
        QVERIFY(!remote.mcpRouteWanted());
        QVERIFY(!remote.tunnelWanted());
    }

    void safeUrlAdmitsWithHostOnlyCookie()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        const quint16 port = startWebRoute(mcp, network, remote, host);
        QVERIFY(port != 0);

        const Resp r = fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8()));
        QCOMPARE_STATUS(r, 303);
        QVERIFY(r.headers.contains("\r\nLocation: /\r\n"));
        QVERIFY(r.headers.contains("; Path=/; Secure; HttpOnly; SameSite=Lax"));
        const QByteArray cookie = admittedCookie(r);
        QVERIFY(!cookie.isEmpty());
        QVERIFY(host.admissions.contains(QString::fromLatin1(cookie)));
    }

    void tokensOpenOnlyTheirOwnRoute()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        McpServer server;
        RemoteAccess remote;
        FakeWebHost host;
        remote.setMcpServer(&server);
        const quint16 port = startWebRoute(mcp, network, remote, host, /*withMcp=*/true);
        QVERIFY(port != 0);

        const QRegularExpression rejected("rejected unauthorized request");
        QTest::ignoreMessage(QtWarningMsg, rejected);
        QCOMPARE(fetch(port, webGet("/web/" + mcp.remoteMcpToken().toUtf8())).status, 0);
        QTest::ignoreMessage(QtWarningMsg, rejected);
        QCOMPARE(fetch(port, httpRequest("POST", "/mcp/" + network.remoteWebToken().toUtf8(),
                                         rpc("initialize", initParams()))).status, 0);
        QTest::ignoreMessage(QtWarningMsg, rejected);
        QCOMPARE(fetch(port, webGet("/web/not-the-real-token")).status, 0);
        QVERIFY(host.admissions.isEmpty());
    }

    void admittedConnectionIsHandedOverUnread()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        const quint16 port = startWebRoute(mcp, network, remote, host);
        const QByteArray cookie = admittedCookie(fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8())));
        QVERIFY(!cookie.isEmpty());

        // The host gets the request still in the socket: ShotServer's parser
        // and the WebSocket upgrade both read it from there.
        QTcpSocket client;
        QVERIFY(sentAndAdopted(port, webGet("/api/session/ws", cookie), host, client));
        QVERIFY(host.firstAdoptedPeek.startsWith("GET /api/session/ws HTTP/1.1"));
        QVERIFY(!remote.m_sockets.contains(host.adopted.constFirst().data()));
    }

    void cookieWithoutAdmissionIsRefused()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        const quint16 port = startWebRoute(mcp, network, remote, host);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected unauthorized request"));
        QTcpSocket client;
        QVERIFY(!sentAndAdopted(port, webGet("/", "forged"), host, client));
        QVERIFY(host.adopted.isEmpty());
    }

    void reachabilityProbeAdmitsNobody()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        const quint16 port = startWebRoute(mcp, network, remote, host);
        QCOMPARE_STATUS(fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8(), {}, "HEAD")), 404);
        QVERIFY(host.admissions.isEmpty());
    }

    void webRouteOffRefusesTheSafeUrl()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        // The connector keeps the listener up once web goes off; refresh() then
        // rebinds it for the Custom-URL connector, so the port is read afresh.
        startWebRoute(mcp, network, remote, host, /*withMcp=*/true);
        network.setRemoteWebEnabled(false);
        QCOMPARE(host.ended, 1);         // sessions end on the switch to off
        const quint16 port = static_cast<quint16>(remote.listenPort());
        QVERIFY(port != 0);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected unauthorized request"));
        QCOMPARE_STATUS(fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8())), 404);
        QVERIFY(host.admissions.isEmpty());
    }

    void webRotationLeavesMcpWorking()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        McpServer server;
        RemoteAccess remote;
        FakeWebHost host;
        remote.setMcpServer(&server);
        const quint16 port = startWebRoute(mcp, network, remote, host, /*withMcp=*/true);
        const QByteArray oldToken = network.remoteWebToken().toUtf8();
        QVERIFY(!admittedCookie(fetch(port, webGet("/web/" + oldToken))).isEmpty());

        QTcpSocket mcpClient;
        QCOMPARE_STATUS(fetch(port, httpRequest("POST", "/mcp/" + mcp.remoteMcpToken().toUtf8(),
                                                rpc("initialize", initParams())), &mcpClient), 200);

        remote.rotateWebToken();
        QCOMPARE(host.ended, 1);
        QVERIFY(host.admissions.isEmpty());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected unauthorized request"));
        QCOMPARE(fetch(port, webGet("/web/" + oldToken)).status, 0);
        // The connector's live connection and token are untouched.
        QCOMPARE_STATUS(fetch(port, httpRequest("POST", "/mcp/" + mcp.remoteMcpToken().toUtf8(),
                                                rpc("initialize", initParams())), &mcpClient), 200);
    }

    void funnelStrangerGetsSilenceOnACustomUrlListener()
    {
        // Remote Web Access starts the Funnel even while the connector is in
        // Custom-URL mode, whose listener is bound for the LAN. The Funnel still
        // arrives from loopback, and a stranger there must learn nothing.
        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        startWebRoute(mcp, network, remote, host, /*withMcp=*/true);
        remote.startListener(/*bindLoopbackOnly=*/false);
        const quint16 port = static_cast<quint16>(remote.listenPort());
        QVERIFY(port != 0);

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("rejected unauthorized request from Funnel"));
        QCOMPARE(fetch(port, httpRequest("GET", "/favicon.ico", {})).status, 0);
    }

    void mcpOffClosesItsConnectionsWhileWebStays()
    {
        SettingsMcp mcp;
        SettingsNetwork network;
        McpServer server;
        RemoteAccess remote;
        FakeWebHost host;
        remote.setMcpServer(&server);
        const quint16 port = startWebRoute(mcp, network, remote, host, /*withMcp=*/true);
        QTcpSocket mcpClient;
        QCOMPARE_STATUS(fetch(port, httpRequest("POST", "/mcp/" + mcp.remoteMcpToken().toUtf8(),
                                                rpc("initialize", initParams())), &mcpClient), 200);
        QCOMPARE(mcpClient.state(), QAbstractSocket::ConnectedState);

        // Driven directly: refresh() would start the embedded node for the web route.
        QObject::disconnect(&mcp, nullptr, &remote, nullptr);
        mcp.setRemoteMcpEnabled(false);
        remote.endRoutesTurnedOff();

        QTRY_COMPARE(mcpClient.state(), QAbstractSocket::UnconnectedState);
        QCOMPARE(host.ended, 0);   // the web route is untouched
        QVERIFY(!admittedCookie(fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8()))).isEmpty());
    }

    void unsupportedMcpModeIsRefusedWhileWebRuns()
    {
        // Remote Web Access keeps the listener up; an ngrok connector, which this
        // build does not serve, must not answer through it.
        SettingsMcp mcp;
        SettingsNetwork network;
        McpServer server;
        RemoteAccess remote;
        FakeWebHost host;
        remote.setMcpServer(&server);
        const quint16 port = startWebRoute(mcp, network, remote, host, /*withMcp=*/true);
        QObject::disconnect(&mcp, nullptr, &remote, nullptr);   // refresh() would start the node
        mcp.setRemoteMcpMode(QString::fromLatin1(SettingsMcp::ModeNgrok));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected unauthorized request"));
        QCOMPARE(fetch(port, httpRequest("POST", "/mcp/" + mcp.remoteMcpToken().toUtf8(),
                                         rpc("initialize", initParams()))).status, 0);
        QVERIFY(remote.connectorUrl().isEmpty());
        QVERIFY(!admittedCookie(fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8()))).isEmpty());
    }

    void lanPeerCannotUseTheWebRoute()
    {
        // A Custom-URL connector binds every interface; the web route still
        // answers only the tunnel's loopback proxy.
        QHostAddress lan;
        for (const QHostAddress& address : QNetworkInterface::allAddresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()
                && !address.isLinkLocal()) {
                lan = address;
                break;
            }
        }
        if (lan.isNull())
            QSKIP("No non-loopback IPv4 address to connect from");

        SettingsMcp mcp;
        SettingsNetwork network;
        RemoteAccess remote;
        FakeWebHost host;
        startWebRoute(mcp, network, remote, host);
        remote.startListener(/*bindLoopbackOnly=*/false);
        const quint16 port = static_cast<quint16>(remote.listenPort());

        QTcpSocket client;
        client.connectToHost(lan, port);
        QElapsedTimer t;
        t.start();
        while (client.state() != QAbstractSocket::ConnectedState && t.elapsed() < 3000
               && client.state() != QAbstractSocket::UnconnectedState)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (client.state() != QAbstractSocket::ConnectedState)
            QSKIP("Could not reach the listener over the LAN address");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("rejected unauthorized request"));
        const Resp r = fetch(port, webGet("/web/" + network.remoteWebToken().toUtf8()), &client);
        QCOMPARE_STATUS(r, 404);   // Mode C answers a refused caller, but admits nobody
        QVERIFY(host.admissions.isEmpty());
    }

    void remoteRouteServesOnlyTheCurrentInterface_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("root") << "/" << true;
        QTest::newRow("root with query") << "/?diagnostics" << true;
        QTest::newRow("client asset") << "/wasm/qtloader.js" << true;
        QTest::newRow("emoji image") << "/wasm/emoji/" + QString(64, QLatin1Char('a')) + "/2615.svg" << true;
        QTest::newRow("map texture") << "/wasm/maps/" + QString(64, QLatin1Char('a')) + "/earth_day.jpg" << true;
        QTest::newRow("session socket") << "/api/session/ws" << true;
        QTest::newRow("bean image") << "/api/session/bean-image/abc" << true;
        QTest::newRow("thumbnail") << "/api/library/thumbnail?id=3" << true;
        QTest::newRow("retired community thumbnail") << "/api/community/thumbnail?id=c4a1dc4a-930b-4f8d-9491-6f3aa9c2bca2&compact=1" << false;
        QTest::newRow("sign-in page") << "/auth/login" << true;
        QTest::newRow("sign-in") << "/api/auth/login" << true;
        QTest::newRow("iOS home-screen icon") << "/apple-touch-icon-precomposed.png" << true;
        QTest::newRow("Android home-screen icon") << "/android-icon-192.png" << true;
        QTest::newRow("unserved icon size") << "/apple-touch-icon-120x120.png" << false;
        QTest::newRow("icon name as a directory") << "/apple-touch-icon/../legacy/x.png" << false;
        QTest::newRow("legacy root") << "/legacy/" << false;
        QTest::newRow("legacy page") << "/legacy/shots" << false;
        QTest::newRow("old bookmark redirect") << "/index.html" << false;
        QTest::newRow("legacy shot list") << "/shots" << false;
        QTest::newRow("legacy AI conversations") << "/ai-conversations" << false;
        QTest::newRow("legacy layout editor") << "/layout" << false;
        QTest::newRow("legacy-only API") << "/api/shots" << false;
        QTest::newRow("device migration") << "/api/backup/manifest" << false;
        QTest::newRow("MCP") << "/mcp" << false;
        QTest::newRow("MCP setup page") << "/mcp/setup" << false;
        QTest::newRow("authenticator reset") << "/api/auth/reset" << false;
        QTest::newRow("traversal") << "/wasm/../legacy/" << false;
    }

    void remoteRouteServesOnlyTheCurrentInterface()
    {
        QFETCH(QString, path);
        QFETCH(bool, allowed);
        QCOMPARE(RemoteWebRoute::isAllowedPath(path), allowed);
    }

    void admissionCookieIsReadFromTheCookieHeader()
    {
        QCOMPARE(RemoteWebRoute::admissionId("GET / HTTP/1.1\r\nCookie: a=1; __Host-decenza_remote=xyz; b=2"),
                 QStringLiteral("xyz"));
        QVERIFY(RemoteWebRoute::admissionId("GET / HTTP/1.1\r\nCookie: decenza_remote=xyz").isEmpty());
        QVERIFY(RemoteWebRoute::admissionId("GET /?__Host-decenza_remote=xyz HTTP/1.1\r\n").isEmpty());
    }

    // ── Turning Remote Access off stops the listener; the local MCP switch does not ─
    void disabledWhenRemoteAccessOff()
    {
        SettingsMcp settings;
        McpServer server;
        RemoteAccess remote;
        const quint16 port = startRemote(settings, server, remote, m_network);
        QVERIFY(port != 0);
        QCOMPARE(remote.statusString(), QStringLiteral("active"));

        settings.setMcpEnabled(false);
        QVERIFY(remote.listenPort() != 0);   // local MCP off leaves Remote Access serving
        settings.setRemoteMcpEnabled(false);
        QCOMPARE(remote.listenPort(), 0);
        QCOMPARE(remote.statusString(), QStringLiteral("off"));
        QVERIFY(remote.connectorUrl().isEmpty());
    }
};

QTEST_MAIN(tst_RemoteAccess)
#include "tst_remoteaccess.moc"
