#include <QtTest>
#include <QSignalSpy>
#include <QPointer>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QHostAddress>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <optional>

#include "ble/scales/decentscalewifi.h"
#include "ble/protocol/decentscaleprotocol.h"
#include "messagecapture.h"

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
                // QWebSocketServer does NOT take ownership of the socket it
                // hands back — unlike QTcpServer, which parents it to itself.
                // Qt's own docs: "It is up to the caller to delete the object
                // explicitly ... otherwise a memory leak will occur."
                //
                // Leaving it unparented leaked one QWebSocket and its internals
                // per connection: measured at 4,681 bytes / 49 allocations per
                // connect-handshake-teardown cycle, scaling EXACTLY 20x between
                // a 1-cycle and a 20-cycle run. It looked like harmless
                // Qt-internal exit noise right up until it was measured.
                //
                // Reparenting to the fake server is enough — it dies with the
                // server at end of scope.
                m_client->setParent(this);
                connect(m_client, &QWebSocket::textMessageReceived,
                        this, [this](const QString& msg) { m_received.append(msg); });
                connect(m_client, &QWebSocket::disconnected,
                        this, &FakeHdsServer::clientDisconnected);
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
    void clientDisconnected();

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
    // ScaleDevice::setConnected(false) emits "[Scale] <name> DISCONNECTED" when
    // a driver that HAD connected is torn down at scope exit — harness noise in
    // every test here, so it is filtered for the whole class.
    //
    // This used to be a QTest::ignoreMessage() in init(). That was subtly wrong:
    // ignoreMessage requires its message to actually fire, and setConnected()
    // only warns on a true->false transition, so any test whose driver never
    // reaches connected failed with "Not all expected messages were received" —
    // a failure with nothing to do with the behaviour under test. Tests that
    // deliberately never connect (an unreachable address, or a pure-function
    // check) are exactly what this change needed to add. A filter allows the
    // warning without demanding it.
    //
    // Kept narrow: only ScaleDevice's own disconnect line. The driver's
    // "WebSocket disconnected (...)" classification logs are separate messages
    // and still reach the handler, so the tests asserting on them are unaffected.
    //
    // std::optional, constructed in init() rather than as a plain member,
    // because ORDER MATTERS: the filter chains to whatever handler is installed
    // when it is constructed. A plain member is constructed with the test
    // object, before Qt Test installs its own handler, so it would capture the
    // default handler and permanently cut Qt Test out of the chain — every
    // ignoreMessage() in the class then fails with "Not all expected messages
    // were received". init() runs after Qt Test's handler is in place.
    std::optional<ScopedWarningFilter> m_disconnectNoise;
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

    // Send a firmware power_off frame and wait until the driver has LOGGED it.
    //
    // This replaces five copies of `sendJson(...); qWait(50);` -- three preceded
    // by an ignoreMessage, two asserting nothing about the log at all -- and it
    // fixes two separate problems. The 50 ms was a fixed wall-clock stand-in for
    // "the frame crossed a real localhost socket and was handled", so a loaded
    // machine loses the race; observed in a full-suite run. And an unmatched
    // ignoreMessage, while it DOES fail the test (qtestresult.cpp:251-254 calls
    // addFailure), reports with no file/line and without naming the pattern --
    // that goes out separately as an Info line (qtestlog.cpp:398-418) -- so the
    // loss surfaced as a bare "Not all expected messages were received" pointing
    // at nothing. Waiting on the message names the line and asserts the tier.
    void sendPowerOffAndAwaitLog(FakeHdsServer& server, const QString& reason,
                                 int reasonCode, QtMsgType expectedTier) {
        MessageCapture capture(MessageCapture::SwallowAll);
        server.sendJson({
            { "type", "power" },
            { "event", "power_off" },
            { "reason", reason },
            { "reason_code", reasonCode },
        });
        const QString needle = QStringLiteral("Scale shut down: ") + reason;
        MessageCapture::Entry logged;
        QTRY_VERIFY2(capture.single(needle, &logged),
                     qPrintable(QStringLiteral("No single \"%1\" line was logged").arg(needle)));
        QCOMPARE(logged.type, expectedTier);
    }

private slots:
    // The "[Scale] <name> DISCONNECTED" teardown warning is handled by the
    // m_disconnectNoise filter declared above, not by an ignoreMessage() —
    // see the note there for why, and for why it must be built here.
    // QtInfoMsg, not QtDebugMsg, on the "WebSocket disconnected (...)"
    // expectations below: the disconnect CLASSIFICATION is why a user's scale
    // dropped, so it sits in the INFO tier that reaches the connections view
    // (see core/logtags.h). The firmware-version lines stay DEBUG — identity
    // detail, not lifecycle.
    void init() {
        QTest::failOnWarning();
        m_disconnectNoise.emplace(QStringLiteral(R"(\[Scale\].*DISCONNECTED)"));
    }
    void cleanup() { m_disconnectNoise.reset(); }

    // ==========================================
    // Snapshot frame parsing
    // ==========================================

    // LEAK-SCALING PROBE. Repeats the full connect/handshake/teardown cycle N
    // times in ONE process, where N comes from DECENZA_LEAK_SCALE (default 1,
    // so an ordinary run is completely unaffected).
    //
    // The question this answers cannot be answered by reading a stack trace.
    // The first nightly ASan run reported 131,068 bytes / 1,372 allocations
    // leaked here, with every frame inside libQt6Core and none in our code —
    // and, notably, ZERO "Direct leak" entries, only "Indirect". That is
    // consistent with bounded state torn down at exit, and equally consistent
    // with a real per-connection leak. The two need different responses
    // (suppress vs fix), and only the growth curve tells them apart:
    //
    //   allocations stay ~1,372 as N rises  -> bounded, one-time, suppressible
    //   allocations scale with N            -> real leak, fix it
    //
    // Symbolizing Qt would say WHERE memory was allocated; it would not say
    // whether the total is bounded. Hence this, which needs no symbols at all.
    void leakScalingProbe() {
        bool ok = false;
        const int n = qEnvironmentVariableIntValue("DECENZA_LEAK_SCALE", &ok);
        const int iterations = (ok && n > 0) ? n : 1;
        qInfo() << "[leak-probe] running" << iterations << "connect/teardown cycles";
        for (int i = 0; i < iterations; ++i) {
            // Any teardown warning is covered by init()'s m_disconnectNoise
            // filter, which ALLOWS the message on every cycle rather than
            // demanding it once — no per-iteration ignoreMessage needed.
            FakeHdsServer server;
            DecentScaleWifi driver;
            connectAndHandshake(driver, server);
            driver.tare();
            QTRY_VERIFY_WITH_TIMEOUT(server.received().size() >= 5, 2000);
        }
        QVERIFY(true);
    }

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

    // The WS firmware no longer auto-pushes status frames, so the driver
    // polls by sending `status` on every Nth base-class keep-alive tick.
    // Cadence must be EXACTLY every 8th call — the BT driver's effective
    // battery poll is kBatteryPollHeartbeatTicks (240) × 1 s = 240 s, and we
    // match that with 8 × 30 s = 240 s. Regression guard against off-by-one
    // (>= vs >, pre- vs post-increment) and against accidental "send every
    // tick" simplifications that would spam the scale 8× too often.
    void keepAliveSendsStatusEveryEighthTick() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);
        server.clearReceived();

        // First seven ticks must NOT send anything.
        for (int i = 0; i < 7; ++i) driver.sendKeepAlive();
        QTest::qWait(100);  // drain any pending socket writes
        QCOMPARE(server.received().size(), 0);

        // Eighth tick triggers the poll.
        driver.sendKeepAlive();
        QTRY_COMPARE(server.received().size(), 1);
        QCOMPARE(server.received().last(), QStringLiteral("status"));

        // Cadence repeats: another seven silent, eighth fires.
        for (int i = 0; i < 7; ++i) driver.sendKeepAlive();
        QTest::qWait(100);
        QCOMPARE(server.received().size(), 1);
        driver.sendKeepAlive();
        QTRY_COMPARE(server.received().size(), 2);
        QCOMPARE(server.received().last(), QStringLiteral("status"));
    }

    // The tick counter must reset on disconnect so a fresh connect cycle
    // doesn't fire `status` early because of carry-over from the prior
    // session. Without the onDisconnected() reset, this test would observe a
    // `status` after only 1 tick on the second connect (8 - 7 prior ticks),
    // not 8. Covers BOTH reset sites — onConnected() alone would mask the bug.
    void keepAliveCounterResetsOnReconnect() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        // Burn 7 ticks on the first connection so any carry-over would be
        // exactly 1 tick away from firing on reconnect.
        for (int i = 0; i < 7; ++i) driver.sendKeepAlive();

        // Drop and reconnect.
        QSignalSpy connSpy(&driver, &ScaleDevice::connectedChanged);
        QTest::ignoreMessage(QtInfoMsg,
            QRegularExpression(QStringLiteral("WebSocket disconnected.*peer close")));
        server.closeFromServer();
        QVERIFY(connSpy.wait(2000));
        QVERIFY(!driver.isConnected());

        // Clear before the second handshake — connectAndHandshake's wait
        // condition is `received().size() >= 4`, which is already true from
        // the first session and would let it return before the new handshake
        // frames actually arrive.
        server.clearReceived();
        connectAndHandshake(driver, server);
        server.clearReceived();

        // After reconnect, the first seven ticks must still be silent.
        for (int i = 0; i < 7; ++i) driver.sendKeepAlive();
        QTest::qWait(100);
        QCOMPARE(server.received().size(), 0);

        driver.sendKeepAlive();
        QTRY_COMPARE(server.received().size(), 1);
        QCOMPARE(server.received().last(), QStringLiteral("status"));

        // This test disconnects twice (the deliberate mid-test drop, then the
        // destructor at scope exit). It used to need a second queued
        // ignoreMessage because init()'s only covered one; m_disconnectNoise
        // filters both, so no per-test handling is needed here any more.
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

    // Abandoning a dial that is still connecting is our own act, not evidence
    // about the address. close() on it raised a Qt write() warning and an
    // "Invalid socket descriptor" error that onError read as "a peer answered":
    // it evicted the cached IP and dialed the hostname for a scale being
    // dropped. Seen in a user log on every WiFi-to-Bluetooth switch and at app
    // exit (both destroy the driver mid-connect). 192.0.2.1 is TEST-NET-1:
    // never routed, so the dial sits in ConnectingState until the OS gives up.
    void teardownMidConnectKeepsCachedIp() {
        QStringList cacheWrites;  // before the driver: its callback writes here
        {
            DecentScaleWifi driver;
            driver.setIpResolver([](const QString&) { return QStringLiteral("192.0.2.1"); });
            driver.setIpCacheUpdate([&](const QString&, const QString& ip) { cacheWrites.append(ip); });

            driver.connectToHost(QStringLiteral("hds.invalid"));
            QTest::qWait(100);  // lets an immediate no-route failure surface
            if (driver.m_socket->state() != QAbstractSocket::ConnectingState)
                QSKIP("192.0.2.1 did not stay in ConnectingState here (no network route?)");

            driver.disconnectFromScale();
            QCoreApplication::processEvents();
            QVERIFY2(cacheWrites.isEmpty(), "disconnectFromScale() mid-connect evicted the cached IP");

            driver.connectToHost(QStringLiteral("hds.invalid"));
            QTRY_COMPARE(driver.m_socket->state(), QAbstractSocket::ConnectingState);
        }  // destroyed mid-connect
        QCoreApplication::processEvents();
        QVERIFY2(cacheWrites.isEmpty(), "~DecentScaleWifi() mid-connect evicted the cached IP");
    }

    // The DE1-sleep check-in exists only to restart the HDS's 15-min auto-off.
    // It must not look like a connection: a command ("display on" from the
    // normal handshake) would light a sleeping scale every 10 min, and a
    // connected flip would swap the app off FlowScale mid-sleep.
    void keepAwakeCheckInIsSilentAndBrief() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        connectAndHandshake(driver, server);
        server.sendJson({{ "grams", 1.5 }, { "ms", 1 }});
        QVERIFY(weightSpy.wait(2000));  // recognized: the address is remembered

        QSignalSpy firstGone(&server, &FakeHdsServer::clientDisconnected);
        driver.disconnectFromScale();
        QVERIFY(firstGone.wait(2000));
        QTRY_VERIFY(!driver.isConnected());
        driver.beginSleepKeepAwake();
        server.clearReceived();

        QSignalSpy driverConnected(&driver, &ScaleDevice::connectedChanged);
        QSignalSpy checkInArrived(&server, &FakeHdsServer::clientConnected);
        QSignalSpy checkInGone(&server, &FakeHdsServer::clientDisconnected);
        driver.checkInToStayAwake();
        QVERIFY(checkInArrived.wait(2000));
        server.sendJson({{ "grams", 1.5 }, { "ms", 2 }});
        QVERIFY(checkInGone.wait(2000));  // closed once the scale answered

        QVERIFY2(server.received().isEmpty(), qPrintable(server.received().join(", ")));
        QCOMPARE(driverConnected.count(), 0);
        driver.endSleepKeepAwake();
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
            QRegularExpression(".*Firmware version: 3\\.0\\.9 \\(reported 'FW: 3\\.0\\.9'\\).*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.0.9" }});
        QTest::qWait(50);

        // Same value — no further log expected. Then a different value warn-logs.
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.0.9" }});
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*Firmware version changed mid-connect: 3\\.0\\.9 -> 3\\.1\\.0.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.1.0" }});
        QTest::qWait(50);
    }

    // The scale sends its compile-time version behind a fixed "FW: " label. The
    // label is stripped by the SAME helper the discovery list uses, so the two
    // views cannot show one scale two different versions. A prerelease suffix is
    // deliberately kept: HdsFirmwareCatalog::parseVersion ignores it when
    // comparing, exactly as the firmware's own comparator does, so nothing
    // downstream needs it gone.
    void firmwareVersionIsNormalizedToMajorMinorPatch_data() {
        QTest::addColumn<QString>("reported");
        QTest::addColumn<QString>("expected");
        QTest::newRow("labelled") << "FW: 3.1.13" << "3.1.13";
        QTest::newRow("prerelease") << "FW: 3.1.14-preview.1" << "3.1.14-preview.1";
        QTest::newRow("unlabelled") << "3.1.13" << "3.1.13";
        QTest::newRow("padded") << "FW:   3.1.13  " << "3.1.13";
        QTest::newRow("no space") << "FW:3.1.13" << "3.1.13";
    }

    void firmwareVersionIsNormalizedToMajorMinorPatch() {
        QFETCH(QString, reported);
        QFETCH(QString, expected);

        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        // Nothing has reported a version yet, so the scale is not a candidate.
        QVERIFY(!driver.supportsFirmwareUpdate());

        // The emit, not the getter. supportsFirmwareUpdate's NOTIFY is this
        // signal, and the update controller re-evaluates availability on it —
        // polling the getter would pass with the emit deleted, leaving a WiFi
        // scale that can never surface an update.
        QSignalSpy versionSpy(&driver, &ScaleDevice::firmwareVersionChanged);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", reported }});
        QTRY_COMPARE(driver.firmwareVersion(), expected);
        QCOMPARE(versionSpy.count(), 1);
        QVERIFY(driver.supportsFirmwareUpdate());

        // And cleared on the way out, or a stale offer outlives the scale.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(".*WebSocket disconnected.*"));
        server.closeFromServer();
        QTRY_VERIFY(!driver.supportsFirmwareUpdate());
        QCOMPARE(versionSpy.count(), 2);
    }

    // WiFi is the third transport the update reaches, and the only one where a
    // connected HDS previously had no update path at all.
    void firmwareUpdateSendsNamedWifiUpdateCommand() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.1.13" }});
        QTRY_VERIFY(driver.supportsFirmwareUpdate());
        server.clearReceived();

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QRegularExpression::escape(
            DecentScaleProtocol::firmwareUpdateStartingMessage(QStringLiteral("3.1.14")))));
        driver.startFirmwareUpdate(QStringLiteral("3.1.14"));
        QTRY_VERIFY(server.received().contains(QStringLiteral("wifi_update 3.1.14")));

        // A bare "wifi_update" is accepted by the firmware and starts its own
        // interactive picker, so an unresolvable target must send nothing. The
        // SAME set the byte transports reject: this path used to count dots, so
        // "3.1.x" and "v3.1.14" went out here and were refused on the other two.
        server.clearReceived();
        // One bad value; the accepted set is asserted by
        // tst_scaleprotocol's hdsTargetVersionEncoding table. This proves only
        // that the driver consults that predicate and sends nothing.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QRegularExpression::escape(
            DecentScaleProtocol::firmwareUpdateBadTargetMessage(QStringLiteral("3.1.x")))));
        driver.startFirmwareUpdate(QStringLiteral("3.1.x"));
        QTest::qWait(50);
        QVERIFY(server.received().isEmpty());
    }

    // The refusal that motivated firmwareUpdateRejected in the first place:
    // an old firmware's command parser doesn't recognize "wifi_update" at
    // all and replies with a generic error frame (real wire shape confirmed
    // against a 3.1.13 scale this session — see #1952).
    void firmwareUpdateRejectedOnErrorFrameReply() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy rejectedSpy(&driver, &ScaleDevice::firmwareUpdateRejected);
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.1.13" }});
        QTRY_VERIFY(driver.supportsFirmwareUpdate());

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(".*Starting firmware update.*"));
        driver.startFirmwareUpdate(QStringLiteral("3.1.14"));
        QTRY_VERIFY(server.received().contains(QStringLiteral("wifi_update 3.1.14")));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*unknown_command.*"));
        server.sendJson({{ "type", "error" }, { "code", "unknown_command" },
                          { "message", "unrecognized or malformed command" }});
        QTRY_COMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.takeFirst().at(0).toString(),
                 QStringLiteral("unrecognized or malformed command"));
    }

    // The accepted path never sends an "error" frame at all — a real ack is a
    // "status" frame shaped exactly like a periodic one (openscale's
    // sendWebsocketStatus). Nothing about that should ever look like a
    // refusal.
    void firmwareUpdateAcceptedReplyIsNotMistakenForARejection() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy rejectedSpy(&driver, &ScaleDevice::firmwareUpdateRejected);
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.1.13" }});
        QTRY_VERIFY(driver.supportsFirmwareUpdate());

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(".*Starting firmware update.*"));
        driver.startFirmwareUpdate(QStringLiteral("3.1.14"));
        QTRY_VERIFY(server.received().contains(QStringLiteral("wifi_update 3.1.14")));

        server.sendJson({{ "type", "status" }, { "status", "ok" },
                          { "firmware_version", "FW: 3.1.13" }});
        QTest::qWait(50);
        QCOMPARE(rejectedSpy.count(), 0);
    }

    // m_awaitingFirmwareUpdateAck must not survive a reconnect: otherwise an
    // error frame on a LATER, unrelated connection could be misattributed to
    // a request from a connection that's already gone.
    void firmwareUpdateAckDoesNotSurviveDisconnect() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy rejectedSpy(&driver, &ScaleDevice::firmwareUpdateRejected);
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(".*Firmware version:.*"));
        server.sendJson({{ "type", "status" }, { "firmware_version", "FW: 3.1.13" }});
        QTRY_VERIFY(driver.supportsFirmwareUpdate());

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(".*Starting firmware update.*"));
        driver.startFirmwareUpdate(QStringLiteral("3.1.14"));
        QTRY_VERIFY(server.received().contains(QStringLiteral("wifi_update 3.1.14")));

        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(".*WebSocket disconnected.*"));
        server.closeFromServer();
        QTRY_VERIFY(!driver.supportsFirmwareUpdate());

        QSignalSpy connectedSpy(&server, &FakeHdsServer::clientConnected);
        driver.connectToHost(server.host());
        QVERIFY(connectedSpy.wait(2000));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*frame_too_large.*"));
        server.sendJson({{ "type", "error" }, { "code", "frame_too_large" },
                          { "message", "unrelated to any update request" }});
        QTest::qWait(50);
        QCOMPARE(rejectedSpy.count(), 0);
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
            QRegularExpression(".*Firmware version: 3\\.0\\.9 \\(reported 'FW: 3\\.0\\.9'\\).*"));
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
        QTest::ignoreMessage(QtInfoMsg,
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
        QTest::ignoreMessage(QtInfoMsg,
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

    void preferredIpDialedAheadOfCacheAndNotPersisted() {
        // A caller with a just-completed mDNS resolution (scan selection / the
        // "Add WiFi Scale" dialog's "Use" button) passes it as preferredIp. It
        // must be dialed directly, AHEAD of the persisted cache, and must NOT be
        // written to the cache — only a verified connect persists, so a stale
        // preferredIp can never clobber a good cached IP (#1603 review follow-up).
        FakeHdsServer fresh;     // the fresh, correct target handed as preferredIp
        SilentServer staleCache; // a stale cache entry that would time out if used
        DecentScaleWifi driver;

        bool updateCalled = false;
        driver.setIpResolver([&](const QString& host) {
            // A (stale) cache entry exists for the hostname.
            return host == QStringLiteral("hds.local") ? staleCache.host() : QString();
        });
        driver.setIpCacheUpdate([&](const QString&, const QString&) { updateCalled = true; });

        QSignalSpy connectedSpy(&fresh, &FakeHdsServer::clientConnected);
        // preferredIp points at the fresh server; the stale cache must be skipped.
        driver.connectToHost(QStringLiteral("hds.local"), fresh.host());

        // Connects to the fresh server well within the 5 s cache-validation
        // timeout — proof the preferredIp was dialed directly, not the cache
        // (a cache dial would hit the silent server and time out first).
        QVERIFY(connectedSpy.wait(2000));
        QTRY_VERIFY_WITH_TIMEOUT(fresh.received().size() >= 4, 2000);

        fresh.sendJson({{ "grams", 25.66 }, { "ms", 12345 }});
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        QVERIFY(weightSpy.wait(500));

        // The unverified preferredIp is never written to the cache.
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
        const qsizetype initialConnections = connectedSpy.count();

        sendPowerOffAndAwaitLog(server, QStringLiteral("low_battery"), 3, QtWarningMsg);
        server.closeFromServer();

        // Wait past the reconnect window (3 s) — should NOT see a reconnect.
        QTest::qWait(3500);
        // After disconnect, exactly one extra connectedChanged (true -> false).
        // No reconnect means no further connectedChanged events.
        QCOMPARE(connectedSpy.count(), initialConnections + 1);
        QVERIFY(!driver.isConnected());
    }

    // After sleep() has armed the app-initiated-power-off latch, the firmware
    // echoes back a power_off frame (reason "disabled", code 0). No
    // errorOccurred should ever fire (the dialog path was removed — see the
    // method-level comment in handlePowerFrame); kept as a regression guard.
    void appInitiatedPowerOffSuppressesDialog() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy errorSpy(&driver, &ScaleDevice::errorOccurred);
        connectAndHandshake(driver, server);

        driver.sleep();  // arms m_powerOffInitiatedByApp
        QTRY_VERIFY(server.received().contains(
            QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}")));

        sendPowerOffAndAwaitLog(server, QStringLiteral("disabled"), 0, QtInfoMsg);

        QCOMPARE(errorSpy.count(), 0);  // errorOccurred not emitted (dialog path removed)
    }

    // The latch is one-shot: after the app-initiated power_off is consumed,
    // a subsequent firmware-initiated power_off (low battery, button) must
    // log at WARN level (not LOG). sendPowerOffAndAwaitLog compares the tier
    // outright — if the latch leaked, the second frame would log at LOG and the
    // QCOMPARE on the entry's type fails, naming both tiers.
    void firmwareInitiatedPowerOffStillWarnsAfterAppInitiated() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy errorSpy(&driver, &ScaleDevice::errorOccurred);
        connectAndHandshake(driver, server);

        // First: app-initiated.
        driver.sleep();
        QTRY_VERIFY(server.received().contains(
            QStringLiteral("{\"command\":\"power\",\"action\":\"off\"}")));
        sendPowerOffAndAwaitLog(server, QStringLiteral("disabled"), 0, QtInfoMsg);

        // Second: firmware-initiated — must log at WARN (latch cleared).
        sendPowerOffAndAwaitLog(server, QStringLiteral("low_battery"), 3, QtWarningMsg);
        QCOMPARE(errorSpy.count(), 0);  // dialog path removed in both cases
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
    //       WARN-level log still fires — the latch did NOT leak past the
    //       failed sleep (a leaked latch would demote the log to LOG and the
    //       tier QCOMPARE inside sendPowerOffAndAwaitLog fails, naming both).
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

        // Now connect and receive a firmware-initiated power_off — must log at WARN.
        FakeHdsServer server;
        connectAndHandshake(driver, server);

        sendPowerOffAndAwaitLog(server, QStringLiteral("low_battery"), 3, QtWarningMsg);
        QCOMPARE(errorSpy.count(), 0);  // dialog path removed
    }

    // When the cached-IP dial is REFUSED (ConnectionRefusedError — a peer
    // answered and rejected the port), the driver must NOT sit on the 5 s
    // recognition timer waiting for it to expire — that is evidence the address
    // was reassigned, so the hostname fallback should run immediately.
    //
    // NOTE: this comment used to list HostNotFoundError, NetworkError and
    // SocketTimeoutError here too, calling them all "transient". Those three
    // now mean the OPPOSITE — see isTransientTransportError() and
    // transportErrorClassification(). Nothing answered in those cases, so the
    // cached IP is retained and the retry is deferred rather than run
    // immediately. Only ConnectionRefusedError still belongs in this test.
    //
    // The user-visible symptom this prevents is on macOS cold start: a cached-IP
    // connect fast-fails against a stale route, and without this fix we wait 5 s,
    // during which BLE-fallback
    // also fails (CoreBluetooth radio not powered on yet), tripping a
    // FlowScale "no scale" dialog before the WiFi scale could just be retried.
    //
    // Test setup: a cached IP that points at a localhost port with no
    // listener (ConnectionRefusedError fires near-instantly), and a real
    // FakeHdsServer for the hostname fallback target.
    // Cached-IP failure must evict the cached entry via m_ipCacheUpdate(host, "")
    // BEFORE falling back to hostname. Without this, a poisoned cache (manually-
    // typed wrong IP, DHCP-reassigned to another device) would survive across
    // reconnect cycles and trigger the WiFi-↔-BLE failover loop in #1281.
    //
    // Asserts the FULL sequence of cache writes, not just the final value:
    // (host, "")  — eviction on cached-IP fail
    // (host, X)   — fresh IP cached after hostname-fallback success
    void cachedIpFailureEvictsCacheBeforeFallback() {
        FakeHdsServer hostnameServer;
        DecentScaleWifi driver;
        driver.setIpResolver([](const QString& /*host*/) {
            // Cached IP points at a port with no listener — instant ConnectionRefused.
            return QStringLiteral("127.0.0.1:1");
        });
        QList<QPair<QString, QString>> cacheWrites;
        driver.setIpCacheUpdate([&](const QString& host, const QString& ip) {
            cacheWrites.append({host, ip});
        });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*WebSocket error.*"));

        QSignalSpy connectedSpy(&hostnameServer, &FakeHdsServer::clientConnected);
        driver.connectToHost(hostnameServer.host());
        QVERIFY(connectedSpy.wait(2000));
        QTRY_VERIFY_WITH_TIMEOUT(hostnameServer.received().size() >= 4, 2000);

        // Drive recognition on the hostname attempt so the fresh-IP cache write
        // fires. Without this, the test would only observe the eviction half.
        hostnameServer.sendJson({{ "grams", 1.0 }, { "ms", 1 }});
        QSignalSpy weightSpy(&driver, &ScaleDevice::weightChanged);
        QVERIFY(weightSpy.wait(500));

        // First write must be the eviction (empty IP). Second must be the
        // fresh peer IP from the successful hostname connect.
        QTRY_COMPARE_WITH_TIMEOUT(cacheWrites.size(), 2, 1000);
        QCOMPARE(cacheWrites[0].first, hostnameServer.host());
        QCOMPARE(cacheWrites[0].second, QString());  // eviction
        QCOMPARE(cacheWrites[1].first, hostnameServer.host());
        QVERIFY(!cacheWrites[1].second.isEmpty());   // re-cache with new IP
    }

    // A cached IP that NOBODY ANSWERED must be kept, not evicted.
    //
    // onRecognitionTimeout fires for two states that need opposite responses: a
    // peer answered and turned out not to be an HDS (evict — the address really is
    // wrong), or nothing answered at all (keep — silence says nothing about the
    // address). Evicting on silence is what turns a scale that was merely
    // rebooting into a permanently unfindable one, and mDNS silence is ordinary:
    // a single query goes unanswered 25-60% of the time on a normal LAN, for
    // every host on the segment. See docs/WIFI_SCALE_MDNS.md. (This comment used
    // to justify the rule with an ARP mechanism; that account is refuted, and the
    // rule never depended on it.)
    //
    // Driven through onRecognitionTimeout directly rather than by waiting out a
    // real 5 s recognition window: the branch under test is chosen by
    // m_wsHandshakeDone alone, and a behavioural version would add five seconds to
    // the suite to reach the same line.
    void silentCachedIpIsKeptAndAnsweringOneIsEvicted_data() {
        QTest::addColumn<bool>("handshakeDone");
        QTest::addColumn<int>("expectedCacheWrites");
        QTest::newRow("nothing answered — keep") << false << 0;
        QTest::newRow("peer answered, not an HDS — evict") << true << 1;
    }

    void silentCachedIpIsKeptAndAnsweringOneIsEvicted() {
        QFETCH(bool, handshakeDone);
        QFETCH(int, expectedCacheWrites);

        DecentScaleWifi driver;
        QList<QPair<QString, QString>> cacheWrites;
        driver.setIpCacheUpdate([&](const QString& host, const QString& ip) {
            cacheWrites.append({host, ip});
        });

        // The state a cached-IP attempt is in when its recognition window expires.
        driver.m_hostname = QStringLiteral("hds.local");
        driver.m_currentTarget = QStringLiteral("192.168.10.145");
        driver.m_currentTargetIsHostname = false;
        driver.m_triedHostnameFallback = false;
        driver.m_wsHandshakeDone = handshakeDone;
        driver.recreateSocket();  // onRecognitionTimeout aborts the socket

        // Only a peer that answered warns; silence is no fault of the address.
        if (handshakeDone)
            QTest::ignoreMessage(QtWarningMsg,
                                 QRegularExpression(QStringLiteral("No recognizable HDS frame.*")));
        driver.onRecognitionTimeout();

        QCOMPARE(cacheWrites.size(), expectedCacheWrites);
        if (expectedCacheWrites > 0) {
            QCOMPARE(cacheWrites[0].first, QStringLiteral("hds.local"));
            QCOMPARE(cacheWrites[0].second, QString());  // eviction writes an empty IP
        }

        // Either way the hostname fallback still runs — keeping the address must
        // not also stop the attempt from progressing.
        QVERIFY(driver.m_pendingHostnameFallback);
    }

    // recognizedAsHds is the single source of truth for "this WS endpoint is
    // really an HDS scale". main.cpp's manual-entry deferred-persistence flow
    // commits addKnownScale/setPrimaryScale/setSavedScaleAddress ONLY when
    // this signal fires. Lock down the contract:
    //   - Fires on first snapshot frame (no "type", "grams" number)
    //   - Fires on first status frame (type=="status")
    //   - Does NOT fire on the WS upgrade alone (no inbound frame yet)
    //   - Fires at most once per attempt (driver's m_recognized latch)
    void recognizedAsHdsFiresOnceOnFirstFrame() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy recognizedSpy(&driver, &DecentScaleWifi::recognizedAsHds);
        connectAndHandshake(driver, server);

        // WS-upgrade alone (no frame received yet): must NOT have fired.
        QCOMPARE(recognizedSpy.count(), 0);

        // First snapshot fires it.
        server.sendJson({{ "grams", 12.34 }, { "ms", 100 }});
        QVERIFY(recognizedSpy.wait(500));
        QCOMPARE(recognizedSpy.count(), 1);

        // Subsequent snapshots do NOT re-emit (the m_recognized latch is the
        // whole point — recognition is per-attempt, not per-frame).
        server.sendJson({{ "grams", 23.45 }, { "ms", 200 }});
        server.sendJson({{ "grams", 34.56 }, { "ms", 300 }});
        QTest::qWait(100);
        QCOMPARE(recognizedSpy.count(), 1);
    }

    // The status-frame path also reaches onRecognizedAsHds. Without this,
    // a scale that happens to send its status frame before any snapshot
    // (the openscale order on connect — see handshake test: 'rate' then
    // 'status') would never validate, and manual entries would dead-end.
    void recognizedAsHdsFiresOnFirstStatusFrame() {
        FakeHdsServer server;
        DecentScaleWifi driver;
        QSignalSpy recognizedSpy(&driver, &DecentScaleWifi::recognizedAsHds);
        connectAndHandshake(driver, server);

        QTest::ignoreMessage(QtDebugMsg,
            QRegularExpression(".*Firmware version: 3\\.0\\.9 \\(reported 'FW: 3\\.0\\.9'\\).*"));
        server.sendJson({
            { "type", "status" },
            { "battery_percent", 80 },
            { "firmware_version", "FW: 3.0.9" },
        });
        QVERIFY(recognizedSpy.wait(500));
        QCOMPARE(recognizedSpy.count(), 1);
    }

    // recognitionFailed is the failure counterpart of recognizedAsHds. It
    // fires from onRecognitionTimeout's terminal "give up THIS attempt"
    // branch when we've already exhausted the hostname fallback (or were on
    // it directly) and 5 s passed with no HDS frame.
    //
    // Without this signal the manual "Add WiFi Scale" flow has a silent-
    // failure window: when the WS handshake succeeds (so onConnected fires →
    // setConnected(true) → the outer 20 s scale-connection-timer is stopped),
    // but no HDS frame arrives, the validation-failed path through
    // onScaleConnectionTimeout never runs. The test below exercises exactly
    // that branch: SilentServer accepts the upgrade and sends nothing.
    void recognitionFailedFiresOnHostnameGiveUp() {
        SilentServer silent;
        DecentScaleWifi driver;
        QSignalSpy recognizedSpy(&driver, &DecentScaleWifi::recognizedAsHds);
        QSignalSpy failedSpy(&driver, &DecentScaleWifi::recognitionFailed);
        // No ipResolver — connectToHost goes straight to attemptHostname,
        // which sets m_currentTargetIsHostname=true. When the recognition
        // timer fires we hit the give-up branch, not the cached-IP-fallback
        // branch.
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*No recognizable HDS frame within.*"));
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*WiFi scale did not respond as HDS.*"));

        driver.connectToHost(silent.host());

        // 5 s recognition window + buffer. recognitionFailed should fire
        // exactly once; recognizedAsHds should NOT fire (no HDS frame arrived).
        QVERIFY(failedSpy.wait(8000));
        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(recognizedSpy.count(), 0);
    }

    // Regression guard against a use-after-free in onRecognitionTimeout's
    // give-up branch: the driver MUST mutate its own state (m_userInitiatedShutdown,
    // m_socket->abort()) BEFORE emitting recognitionFailed. Why: in production
    // main.cpp wires the signal to a slot that emits disconnectScaleRequested,
    // whose handler synchronously calls physicalScale.reset() — destroying
    // `this` while onRecognitionTimeout is still on the stack. Any post-emit
    // access to a member would be UB. The fix is to do all member writes
    // first and emit last; this test exercises the destroy-during-emit
    // pattern so a future re-ordering tripwire is caught by ASan / a flaky
    // crash here.
    //
    // We delete the driver INSIDE the recognitionFailed slot to reproduce
    // the synchronous-destroy chain. If the driver still touches `this`
    // after the emit, the read/write is on freed memory.
    void recognitionFailedSlotMaySafelyDestroyDriver() {
        SilentServer silent;
        auto* driver = new DecentScaleWifi();  // raw — we own deletion
        QPointer<DecentScaleWifi> guard(driver);

        bool slotFired = false;
        QObject::connect(driver, &DecentScaleWifi::recognitionFailed, this,
            [&driver, &slotFired]() {
                slotFired = true;
                delete driver;       // synchronous destroy — same as production
                driver = nullptr;    // for hygiene; the test doesn't reuse it
            },
            Qt::DirectConnection);   // pin synchronous; AutoConnection would resolve the same way

        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*No recognizable HDS frame within.*"));
        QTest::ignoreMessage(QtWarningMsg,
            QRegularExpression(".*WiFi scale did not respond as HDS.*"));

        driver->connectToHost(silent.host());

        // Wait past the 5 s recognition window for the give-up branch to fire.
        // If member writes happen AFTER the emit, the test crashes (UAF) or
        // ASan reports a use-after-free. If the order is correct, the slot
        // runs cleanly, the object is destroyed exactly once, and we observe
        // it via the QPointer.
        QTRY_VERIFY_WITH_TIMEOUT(slotFired, 8000);
        QVERIFY(guard.isNull());  // QPointer auto-nulls on QObject destruction
    }

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

    // ==========================================
    // Transient vs wrong-host failure classification
    // ==========================================

    // The classifier is the whole fix in one function, so pin the values it
    // reasons about explicitly plus representatives of the default bucket. It
    // does NOT cover all 24 SocketError enumerators — the untested ones all
    // fall through to non-transient by design. The question it answers is
    // "did ANY peer answer", not "did the attempt fail".
    //
    // TWO entries are counter-intuitive and are the ones a future reader is
    // most likely to "correct". Both are deliberate:
    //
    //   ConnectionRefusedError -> NOT transient. A TCP RST usually means a host
    //     is up and refused the port: the DHCP-reassignment evidence the
    //     hostname fallback acts on. cachedIpFailureEvictsCacheBeforeFallback
    //     and cachedIpFastFailsToHostnameFallback both depend on this and would
    //     fail if it moved.
    //
    //   HostNotFoundError -> NOT transient, despite "the name didn't resolve"
    //     sounding like nothing answered. It can only reach onError from a bare
    //     hostname dial, and a hostname attempt was already excluded from the
    //     cached-IP eviction branch — so classifying it transient protects no
    //     cache decision. Its only effect would be to suppress the give-up path
    //     that raises the manual "Add WiFi Scale" failure dialog, leaving a user
    //     who typed a bad hostname with no feedback at all.
    void transportErrorClassification() {
        // Nothing answered — carries no evidence about who owns the address.
        // NetworkError is the load-bearing one: Qt maps EHOSTUNREACH,
        // ENETUNREACH and ETIMEDOUT all onto it.
        QVERIFY(DecentScaleWifi::isTransientTransportError(QAbstractSocket::NetworkError));
        QVERIFY(DecentScaleWifi::isTransientTransportError(QAbstractSocket::SocketTimeoutError));

        // Something answered, or the failure says nothing useful about the
        // cached IP — either way, not transient.
        QVERIFY(!DecentScaleWifi::isTransientTransportError(QAbstractSocket::HostNotFoundError));
        QVERIFY(!DecentScaleWifi::isTransientTransportError(QAbstractSocket::ConnectionRefusedError));
        QVERIFY(!DecentScaleWifi::isTransientTransportError(QAbstractSocket::RemoteHostClosedError));
        QVERIFY(!DecentScaleWifi::isTransientTransportError(QAbstractSocket::UnknownSocketError));
        QVERIFY(!DecentScaleWifi::isTransientTransportError(QAbstractSocket::OperationError));
    }

    // The re-resolve must not be able to leave the driver dialling nothing.
    // When resolution fails, dialCachedIpAfterResolveFailure() falls back to the
    // cached IP — otherwise a device whose mDNS is unreliable (exactly what the
    // IP cache exists for) would spend every reconnect cycle resolving, failing,
    // and opening no socket at all.
    //
    // This path had NO coverage before: every other test uses a
    // "127.0.0.1:<port>" hostname, which is not ".local", so attemptHostname()
    // dials it directly and never runs a resolution that could fail. Only a
    // ".local" name that cannot resolve reaches the fallback.
    void resolveFailureFallsBackToCachedIp() {
        // Narrow to the two resolution-failure warnings this test actually
        // expects (the QHostInfo branch on desktop, the mDNS branch on Android)
        // rather than anything containing "resolution failed" — a broader
        // pattern would silently absorb an unrelated failure firing instead.
        ScopedWarningFilter resolveNoise{  // declared before `driver`
            QStringLiteral("(mDNS|QHostInfo) resolution failed")};
        FakeHdsServer server;
        DecentScaleWifi driver;
        driver.setIpResolver([&](const QString&) { return server.host(); });

        QSignalSpy connectedSpy(&server, &FakeHdsServer::clientConnected);

        // A .local name that will not resolve, so attemptHostname()'s resolve
        // branch fails and the fallback is the only way a socket gets opened.
        // Reached via the re-resolve flag rather than the plain cached-IP path:
        // connectToHost consults the cache first unless the flag is set.
        driver.m_retryShouldReresolve = true;
        driver.connectToHost(QStringLiteral("decenza-nonexistent-xyz.local"));

        // Generous timeout: resolution latency for a missing .local varies a lot
        // by platform (fast where no responder is installed, 1-2 s with avahi or
        // mDNSResponder), and the fallback only runs once it has given up.
        QVERIFY2(connectedSpy.wait(15000),
                 "Resolution failed and no socket was opened — the cached-IP "
                 "fallback did not run, so this cycle dialled nothing");
        QTRY_VERIFY_WITH_TIMEOUT(server.received().size() >= 4, 2000);
    }
};

QTEST_GUILESS_MAIN(tst_DecentScaleWifi)
#include "tst_decentscalewifi.moc"
