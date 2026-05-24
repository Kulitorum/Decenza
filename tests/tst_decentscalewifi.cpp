#include <QtTest>
#include <QSignalSpy>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QHostAddress>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>

#include "ble/scales/decentscalewifi.h"

// Fake WebSocket server used to drive the DecentScaleWifi driver under test.
// Spins up on a random local port; the test connects the driver to
// ws://127.0.0.1:<port>/snapshot and verifies frames in both directions.
class FakeHdsServer : public QObject {
    Q_OBJECT
public:
    FakeHdsServer() : m_server(new QWebSocketServer(QStringLiteral("FakeHds"),
                                                    QWebSocketServer::NonSecureMode, this))
    {
        QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));
        connect(m_server, &QWebSocketServer::newConnection, this, [this]() {
            while (m_server->hasPendingConnections()) {
                m_client = m_server->nextPendingConnection();
                connect(m_client, &QWebSocket::textMessageReceived,
                        this, [this](const QString& msg) { m_received.append(msg); });
                emit clientConnected();
            }
        });
    }

    QUrl url(const QString& path = QStringLiteral("/snapshot")) const {
        return QUrl(QStringLiteral("ws://127.0.0.1:%1%2").arg(m_server->serverPort()).arg(path));
    }

    QString host() const {
        return QStringLiteral("127.0.0.1:%1").arg(m_server->serverPort());
    }

    void send(const QString& text) {
        QVERIFY(m_client);
        m_client->sendTextMessage(text);
        m_client->flush();
    }

    void sendJson(const QJsonObject& obj) {
        send(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }

    void closeFromServer() {
        if (m_client) m_client->close();
    }

    // Abrupt TCP teardown (no WS close handshake) — the client sees a socket
    // error, simulating an RF/WiFi/network drop.
    void abortClient() {
        if (m_client) m_client->abort();
    }

    const QStringList& received() const { return m_received; }
    void clearReceived() { m_received.clear(); }

signals:
    void clientConnected();

private:
    QWebSocketServer* m_server = nullptr;
    QWebSocket* m_client = nullptr;
    QStringList m_received;
};

// Accepts a WS upgrade but never sends any frame — simulates a non-HDS
// WebSocket server sitting at a cached IP after DHCP reassignment.
class SilentServer : public QObject {
    Q_OBJECT
public:
    SilentServer() : m_server(new QWebSocketServer(QStringLiteral("Silent"),
                                                   QWebSocketServer::NonSecureMode, this))
    {
        m_server->listen(QHostAddress::LocalHost, 0);
        connect(m_server, &QWebSocketServer::newConnection, this, [this]() {
            while (m_server->hasPendingConnections()) {
                auto* c = m_server->nextPendingConnection();
                c->setParent(this);
                emit clientConnected();
            }
        });
    }
    QString host() const {
        return QStringLiteral("127.0.0.1:%1").arg(m_server->serverPort());
    }
signals:
    void clientConnected();
private:
    QWebSocketServer* m_server = nullptr;
};

class tst_DecentScaleWifi : public QObject {
    Q_OBJECT

private:
    // Wait for the driver to connect to the fake server and the initial
    // handshake messages ("rate 10k", "events on", "status", "display on") to
    // arrive. QTRY_VERIFY actively polls so the timeout absorbs scheduler
    // jitter between client `connected` and server `textMessageReceived`.
    void connectAndHandshake(DecentScaleWifi& driver, FakeHdsServer& server) {
        QSignalSpy connectedSpy(&server, &FakeHdsServer::clientConnected);
        driver.connectToHost(server.host());
        QVERIFY(connectedSpy.wait(2000));
        QTRY_VERIFY_WITH_TIMEOUT(server.received().size() >= 4, 2000);
    }

private slots:
    // Suppress the expected "[Scale] <name> DISCONNECTED" warning from
    // ScaleDevice::setConnected(false) that fires when the test's driver
    // is torn down at scope exit.
    void init() {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*DISCONNECTED.*"));
    }

    // ==========================================
    // Snapshot frame parsing
    // ==========================================

    void weightSnapshotEmitsSetWeight() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        connectAndHandshake(driver, server);

        server.sendJson({{ "grams", 25.66 }, { "ms", 12345 }});
        QVERIFY(weightSpy.wait(500));
        QCOMPARE(weightSpy.last().at(0).toDouble(), 25.66);
    }

    void malformedSnapshotIsDroppedSilently() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        connectAndHandshake(driver, server);

        server.send(QStringLiteral("not-json"));
        QTest::qWait(100);
        QCOMPARE(weightSpy.count(), 0);
    }

    // ==========================================
    // Connect-time handshake
    // ==========================================

    void connectSendsRateAndEventsOn() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        // After handshake, received() carries the four setup frames in order.
        // The "display on" frame restores LCD state on reconnect after the
        // DE1-sleep keepScaleOn=true+WiFi graceful-close path turned it off;
        // dropping it from onConnected() would silently leave the scale dark
        // every time the DE1 wakes — this assertion is the regression guard.
        const QStringList rx = server.received();
        QCOMPARE(rx.size(), 4);
        QCOMPARE(rx[0], QStringLiteral("rate 10k"));
        QCOMPARE(rx[1], QStringLiteral("events on"));
        QCOMPARE(rx[2], QStringLiteral("status"));
        QCOMPARE(rx[3], QStringLiteral("display on"));
    }

    // ==========================================
    // Command surface
    // ==========================================

    void tareSendsTextFrame() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);
        server.clearReceived();

        driver.tare();
        QTRY_VERIFY(server.received().contains(QStringLiteral("tare")));
    }

    void timerCommandsSendCorrectFrames() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);
        server.clearReceived();

        driver.startTimer();
        driver.stopTimer();
        driver.resetTimer();
        QTRY_COMPARE(server.received().size(), 3);
        QCOMPARE(server.received()[0], QStringLiteral("timer start"));
        QCOMPARE(server.received()[1], QStringLiteral("timer stop"));
        QCOMPARE(server.received()[2], QStringLiteral("timer reset"));
    }

    void displayCommandsSendCorrectFrames() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);
        server.clearReceived();

        driver.disableLcd();
        driver.wake();
        QTRY_COMPARE(server.received().size(), 3);
        QCOMPARE(server.received()[0], QStringLiteral("display off"));
        // wake() restores sensors first, then OLED.
        QCOMPARE(server.received()[1], QStringLiteral("soft_sleep off"));
        QCOMPARE(server.received()[2], QStringLiteral("display on"));
    }

    void sleepSendsPowerOffAndEmitsSleepCompleted() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy sleepSpy(&driver, &ScaleDevice::sleepCompleted);
        connectAndHandshake(driver, server);
        server.clearReceived();

        driver.sleep();
        // sleep() must send the firmware power-off JSON, the WS analog of BT's
        // 0A 02 00 — NOT soft_sleep on, which is wake()'s reversible-park
        // complement and leaves the ESP32 radio active draining a battery-only
        // HDS. The negative assertion guards against a revert to the earlier
        // wrong mapping.
        QTRY_VERIFY(server.received().contains(
            QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}")));
        QVERIFY(!server.received().contains(QStringLiteral("soft_sleep on")));
        QCOMPARE(sleepSpy.count(), 1);
    }

    void setLedFormatsAndClamps() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);
        server.clearReceived();

        driver.setLed(255, 128, 0);
        driver.setLed(300, -5, 256);  // Clamps to 255 / 0 / 255.
        QTRY_COMPARE(server.received().size(), 2);
        QCOMPARE(server.received()[0], QStringLiteral("led 255 128 0"));
        QCOMPARE(server.received()[1], QStringLiteral("led 255 0 255"));
    }

    // ==========================================
    // Status frame
    // ==========================================

    void statusFrameUpdatesBatteryAndCharging() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy battSpy(&driver, &ScaleDevice::batteryLevelChanged);
        QSignalSpy chargeSpy(&driver, &ScaleDevice::chargingChanged);
        connectAndHandshake(driver, server);

        server.sendJson({
            { "type", "status" },
            { "battery_percent", 82 },
            { "charging", false },
        });
        QVERIFY(battSpy.wait(500));
        QCOMPARE(battSpy.last().at(0).toInt(), 82);
        // charging defaulted to false; staying false should NOT emit again.
        QCOMPARE(chargeSpy.count(), 0);

        // Toggle to true.
        server.sendJson({{ "type", "status" }, { "charging", true }});
        QVERIFY(chargeSpy.wait(500));
        QCOMPARE(chargeSpy.last().at(0).toBool(), true);

        // Toggle back to false.
        server.sendJson({{ "type", "status" }, { "charging", false }});
        QTRY_COMPARE(chargeSpy.count(), 2);
        QCOMPARE(chargeSpy.last().at(0).toBool(), false);
    }

    void firmwareVersionLogsOnceAndWarnsOnChange() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: FW: 3\\.0\\.9.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.0.9" }});
        QTest::qWait(50);

        // Same value — no further log expected. Then a different value warn-logs.
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.0.9" }});
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*Firmware version changed mid-connect: FW: 3\\.0\\.9 -> FW: 3\\.1\\.0.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.1.0" }});
        QTest::qWait(50);
    }

    // Regression: the real firmware's status frame ALSO carries a `grams` field
    // (openscale README). Snapshots are distinguished by the ABSENCE of `type`,
    // so a status-with-grams must still reach the status handler — keying on the
    // presence of `grams` (the old bug) swallowed it as a weight snapshot, so
    // battery / charging / firmware_version were never parsed over WiFi.
    void statusFrameWithGramsIsNotMistakenForSnapshot() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy battSpy(&driver, &ScaleDevice::batteryLevelChanged);
        QSignalSpy chargeSpy(&driver, &ScaleDevice::chargingChanged);
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: FW: 3\\.0\\.9.*"));
        server.sendJson({
            { "type", "status" },
            { "grams", 25.66 },
            { "battery_percent", 77 },
            { "charging", true },
            { "firmware_version", "FW: 3.0.9" },
        });

        // Routed to the status handler: battery, charging, and firmware_version
        // all parse from the same frame...
        QVERIFY(battSpy.wait(500));
        QCOMPARE(battSpy.last().at(0).toInt(), 77);
        QVERIFY(chargeSpy.count() >= 1);
        QCOMPARE(chargeSpy.last().at(0).toBool(), true);
        // ...and it is NOT double-handled as a weight snapshot.
        QCOMPARE(weightSpy.count(), 0);
    }

    // A clean close frame from the scale (no socket error, not app-initiated) is
    // classified as a "peer close" — the one case where closeCode()/closeReason()
    // are trustworthy. Guards the reworked disconnect classification that no
    // longer relies on closeCode() to detect abnormal drops.
    void unexpectedPeerCloseIsClassifiedAsPeerClose() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        QSignalSpy connSpy(&driver, &ScaleDevice::connectedChanged);
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(QStringLiteral("WebSocket disconnected \\(unexpected\\).*peer close")));
        server.closeFromServer();
        QVERIFY(connSpy.wait(2000));
        QVERIFY(!driver.isConnected());
    }

    // An abrupt TCP teardown (no WS close handshake) surfaces on the client as a
    // socket error and must be logged as an abnormal transport drop — NOT
    // "(expected)" and NOT "peer close". Guards the fix where a genuine transport
    // error forces the "(unexpected)" prefix regardless of m_userInitiatedShutdown.
    void abruptDropIsClassifiedAsTransportError() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        QSignalSpy connSpy(&driver, &ScaleDevice::connectedChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("WebSocket error:")));
        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(QStringLiteral("WebSocket disconnected \\(unexpected\\).*transport error")));
        server.abortClient();
        QVERIFY(connSpy.wait(2000));
        QVERIFY(!driver.isConnected());
    }

    // ==========================================
    // Button event encoding
    // ==========================================

    void buttonFrameEmitsEncodedSignal() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy buttonSpy(&driver, &ScaleDevice::buttonPressed);
        connectAndHandshake(driver, server);

        server.sendJson({
            { "type", "button" },
            { "button_number", 1 },
            { "press_code", 1 },
        });
        QVERIFY(buttonSpy.wait(500));
        // Encoding: 0x1000 | (button << 8) | press_code = 0x1101 for circle-short.
        QCOMPARE(buttonSpy.last().at(0).toInt(), 0x1101);

        server.sendJson({
            { "type", "button" },
            { "button_number", 2 },
            { "press_code", 2 },
        });
        QTRY_COMPARE(buttonSpy.count(), 2);
        QCOMPARE(buttonSpy.last().at(0).toInt(), 0x1202);  // square-long
    }

    // ==========================================
    // mDNS resilience: cache + hostname fallback
    // ==========================================

    void successfulHostnameConnectCachesPeerIp() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QString cachedKey, cachedIp;
        driver.setIpResolver([](const QString&) { return QString(); });
        driver.setIpCacheUpdate([&](const QString& host, const QString& ip) {
            cachedKey = host; cachedIp = ip;
        });

        connectAndHandshake(driver, server);  // Connects via the host arg (acts like a hostname here).

        // Recognition fires on inbound frames — send a snapshot to validate.
        server.sendJson({{ "grams", 25.66 }, { "ms", 12345 }});
        QTRY_VERIFY_WITH_TIMEOUT(!cachedKey.isEmpty(), 1000);
        QCOMPARE(cachedKey, server.host());
        // Peer IP is whatever 127.0.0.1 resolves to from the WS — non-empty.
        QVERIFY(!cachedIp.isEmpty());
    }

    void cachedIpHitConnectsDirectly() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        // Resolver returns the server's host directly so attemptTarget uses
        // the "cached IP" branch (isHostname=false). No update should fire
        // because we used the cache, not the hostname.
        bool updateCalled = false;
        driver.setIpResolver([&](const QString& host) {
            // Pretend the cache hit is the same host:port the test server uses.
            return host == QStringLiteral("hds.local") ? server.host() : QString();
        });
        driver.setIpCacheUpdate([&](const QString&, const QString&) { updateCalled = true; });

        QSignalSpy connectedSpy(&server, &FakeHdsServer::clientConnected);
        driver.connectToHost(QStringLiteral("hds.local"));
        QVERIFY(connectedSpy.wait(2000));
        QTRY_VERIFY_WITH_TIMEOUT(server.received().size() >= 4, 2000);

        // Send a snapshot so recognition fires.
        server.sendJson({{ "grams", 25.66 }, { "ms", 12345 }});
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        QVERIFY(weightSpy.wait(500));

        // Cache used as-is; no update should be written (cache was already correct).
        QTest::qWait(100);
        QCOMPARE(updateCalled, false);
    }

    void cachedIpValidationTimeoutFallsBackToHostname() {
        // The "cached IP" resolves to a silent server; the "hostname" resolves
        // to a real fake HDS. Driver should validate-time-out on the silent
        // server and fall back to the hostname, where it succeeds.
        SilentServer silent;
        FakeHdsServer real;
        DecentScaleWifi driver;
        driver.setIpResolver([&](const QString& host) {
            return host == real.host() ? silent.host() : QString();
        });
        QString cachedIpAfter;
        driver.setIpCacheUpdate([&](const QString&, const QString& ip) {
            cachedIpAfter = ip;
        });

        // Override the recognition timeout to make the test fast.
        // (Driver uses 5 s; we can't reduce it from outside without an API.
        //  Instead just wait — the test costs 5 s, which is acceptable for the
        //  one path that genuinely exercises the timeout-and-fallback flow.)
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*No recognizable HDS frame within.*"));

        QSignalSpy realConnectedSpy(&real, &FakeHdsServer::clientConnected);
        driver.connectToHost(real.host());

        // Wait for the silent server to receive the upgrade attempt, then for
        // the 5 s recognition timeout to fire and the driver to fall back.
        QVERIFY(realConnectedSpy.wait(8000));  // 5s timeout + fallback margin

        // Now the fallback (hostname) is active — send a snapshot so it validates.
        QTRY_VERIFY_WITH_TIMEOUT(real.received().size() >= 4, 2000);
        real.sendJson({{ "grams", 12.34 }, { "ms", 1000 }});
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        QVERIFY(weightSpy.wait(500));

        // Cache update fired with the real peer IP (not the silent server).
        QVERIFY(!cachedIpAfter.isEmpty());
    }

    // ==========================================
    // Power event — suppresses reconnect
    // ==========================================

    void powerEventSuppressesReconnect() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy connectedSpy(&driver, &ScaleDevice::connectedChanged);
        connectAndHandshake(driver, server);
        const int initialConnections = connectedSpy.count();

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*Scale shut down: low_battery.*"));
        server.sendJson({
            { "type", "power" },
            { "event", "power_off" },
            { "reason", "low_battery" },
            { "reason_code", 3 },
        });
        QTest::qWait(50);
        server.closeFromServer();

        // Wait past the reconnect window (3 s) — should NOT see a reconnect.
        QTest::qWait(3500);
        // After disconnect, exactly one extra connectedChanged (true -> false).
        // No reconnect means no further connectedChanged events.
        QCOMPARE(connectedSpy.count(), initialConnections + 1);
        QVERIFY(!driver.isConnected());
    }

    // After sleep() has armed the app-initiated-power-off latch, the firmware
    // echoes back a power_off frame (reason "disabled", code 0). The user
    // already knows — we just told the scale to power off — so the dialog
    // (errorOccurred) must be suppressed.
    void appInitiatedPowerOffSuppressesDialog() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy errorSpy(&driver, &ScaleDevice::errorOccurred);
        connectAndHandshake(driver, server);

        driver.sleep();  // arms m_powerOffInitiatedByApp
        QTRY_VERIFY(server.received().contains(
            QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}")));

        server.sendJson({
            { "type", "power" },
            { "event", "power_off" },
            { "reason", "disabled" },
            { "reason_code", 0 },
        });
        QTest::qWait(50);

        QCOMPARE(errorSpy.count(), 0);  // dialog suppressed
    }

    // The latch is one-shot: after the app-initiated power_off is consumed,
    // a subsequent firmware-initiated power_off (low battery, button) must
    // still surface a dialog. Without explicit clearing, a sleep() whose
    // echo got lost could silently swallow a real shutdown later.
    void firmwareInitiatedPowerOffStillDialogsAfterAppInitiated() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy errorSpy(&driver, &ScaleDevice::errorOccurred);
        connectAndHandshake(driver, server);

        // First: app-initiated, suppressed.
        driver.sleep();
        QTRY_VERIFY(server.received().contains(
            QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}")));
        server.sendJson({
            { "type", "power" },
            { "event", "power_off" },
            { "reason", "disabled" },
            { "reason_code", 0 },
        });
        QTest::qWait(50);
        QCOMPARE(errorSpy.count(), 0);

        // Second: firmware-initiated, must dialog. Latch already cleared by
        // the first frame's consume-and-clear.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*Scale shut down: low_battery.*"));
        server.sendJson({
            { "type", "power" },
            { "event", "power_off" },
            { "reason", "low_battery" },
            { "reason_code", 3 },
        });
        QTest::qWait(50);
        QCOMPARE(errorSpy.count(), 1);
    }

    // sleep() called while the socket is not connected (common at app exit
    // after a DE1-sleep keepScaleOn=true+WiFi close — the WS is already in
    // ClosingState when sleep() runs): the power-off command isn't delivered,
    // so no firmware echo will arrive. Verify the observable end-to-end
    // behaviour:
    //   (a) sleep() still completes (sleepCompleted emitted) so app-exit
    //       waitLoops don't hang
    //   (b) the "not delivered" WARN fires for diagnostics
    //   (c) after reconnecting and receiving a real firmware power_off, the
    //       dialog still fires — the latch did NOT leak past the failed
    //       sleep.
    // Note: this test can't isolate which clear-path did the work — both
    // sleep()'s self-clear-on-failure AND connectToHost()'s reset would
    // independently produce (c). Both are present in the implementation as
    // defense-in-depth; the test only asserts the combined invariant.
    void sleepWithDisconnectedSocketDoesNotLeakLatch() {
        DecentScaleWifi driver;
        QSignalSpy sleepSpy(&driver, &ScaleDevice::sleepCompleted);
        QSignalSpy errorSpy(&driver, &ScaleDevice::errorOccurred);
        // No connectAndHandshake() — socket starts in UnconnectedState, so
        // send() returns false synchronously.

        // sleep() in this branch fires TWO warnings: send()'s own
        // "send() dropped — socket not connected" (because we never
        // connected), then sleep()'s "power-off command not delivered".
        // Order matters for ignoreMessage — first match consumes first
        // emit.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*send\\(\\) dropped.*"));
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*power-off command not delivered.*"));
        driver.sleep();
        QCOMPARE(sleepSpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        // Now connect and receive a firmware-initiated power_off — must dialog.
        FakeHdsServer server;
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*Scale shut down: low_battery.*"));
        server.sendJson({
            { "type", "power" },
            { "event", "power_off" },
            { "reason", "low_battery" },
            { "reason_code", 3 },
        });
        QTest::qWait(50);
        QCOMPARE(errorSpy.count(), 1);
    }

    // When the cached-IP dial fires a fatal transient socket error
    // (HostNotFoundError, ConnectionRefusedError, NetworkError, SocketTimeoutError),
    // the driver must NOT sit on the 5 s recognition timer waiting for it to
    // expire — the kernel just told us the cached IP is dead, so the hostname
    // fallback should run immediately. The user-visible symptom this prevents
    // is on macOS cold start: ARP/route stale at .242 → cached-IP connect
    // fast-fails → without this fix we wait 5 s, during which BLE-fallback
    // also fails (CoreBluetooth radio not powered on yet), tripping a
    // FlowScale "no scale" dialog before the WiFi scale could just be retried.
    //
    // Test setup: a cached IP that points at a localhost port with no
    // listener (ConnectionRefusedError fires near-instantly), and a real
    // FakeHdsServer for the hostname fallback target.
    void cachedIpFastFailsToHostnameFallback() {
        FakeHdsServer hostnameServer;
        DecentScaleWifi driver;
        // The cached IP resolver returns a port with no listener — connect
        // will fail fast with ConnectionRefusedError.
        driver.setIpResolver([](const QString& /*host*/) {
            return QStringLiteral("127.0.0.1:1");  // Port 1: no listener
        });
        QSignalSpy connectedSpy(&hostnameServer, &FakeHdsServer::clientConnected);
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*WebSocket error.*"));

        // Dial via the hostname target (which the FakeHdsServer is listening on).
        QElapsedTimer timer;
        timer.start();
        driver.connectToHost(hostnameServer.host());

        // The hostname-fallback connect must succeed FAR sooner than the 5 s
        // recognition timeout — give it a generous 2 s budget to absorb CI jitter.
        // Pre-fix this would have taken ~5 s.
        QVERIFY(connectedSpy.wait(2000));
        // Also wait for the driver's handshake messages to land on the server so
        // setConnected(true) has definitely fired — otherwise init()'s expected
        // DISCONNECTED warning won't fire at scope exit and the test fails on
        // "Did not receive any message matching".
        QTRY_VERIFY_WITH_TIMEOUT(hostnameServer.received().size() >= 4, 2000);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < 4000,
            qPrintable(QString("Hostname fallback took %1 ms — must be far less than "
                               "the 5 s recognition timeout").arg(elapsed)));
    }
};

QTEST_GUILESS_MAIN(tst_DecentScaleWifi)
#include "tst_decentscalewifi.moc"
