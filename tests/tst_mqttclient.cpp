#include <QtTest>
#include <QSignalSpy>

#include "core/settings.h"
#include "core/settings_mqtt.h"
#include "network/mqttclient.h"

// Reconnect state machine for MqttClient.
//
// Why this file exists: mqttclient.cpp was in NO test target at all, and a change whose
// entire purpose was to stop MQTT reconnection from dying permanently shipped a latched
// m_userRequestedDisconnect that reintroduced exactly that death — silently, with no
// warning and no status change. Nothing in a 101-test suite could have caught it.
//
// Everything here drives the private slots directly through the DECENZA_TESTING friend
// declaration and asserts on QTimer state. No broker, no sockets, no waiting: the state
// machine is pure logic over a timer and a handful of flags, and the Paho callbacks it
// reacts to are already marshalled onto these slots via queued connections.
class tst_MqttClient : public QObject {
    Q_OBJECT

private:
    // device and machineState are only used to wire telemetry signals; the reconnect
    // machine touches neither, so nullptr keeps the fixture to Settings alone.
    static MqttClient* makeClient(Settings& settings) {
        return new MqttClient(nullptr, nullptr, &settings, settings.mqtt());
    }

    // Put the client in the state the app is in with MQTT switched on and a host set,
    // which is what every reconnect path assumes.
    static void enableMqtt(Settings& settings) {
        settings.mqtt()->setMqttEnabled(true);
        settings.mqtt()->setMqttBrokerHost(QStringLiteral("192.0.2.1"));  // TEST-NET-1
    }

private slots:
    void init() { QTest::failOnWarning(); }

    // ===== The regression this file was created for =====

    void disconnectWhileNotConnectedDoesNotStrandTheNextRealDrop() {
        // THE bug. disconnectFromBroker() used to set m_userRequestedDisconnect
        // unconditionally, but only the connected branch produces the callback that
        // consumes it. Disconnecting while already disconnected therefore latched the
        // flag forever, and the next GENUINE broker drop was read as user-requested:
        // timer stopped, nothing armed, MQTT dead until app restart.
        //
        // Reachable from the Home Automation tab's Disconnect button and from
        // ShotServer::handleMqttDisconnect, neither of which checks isConnected() —
        // and tapping Disconnect while the status reads "reconnecting (3/10)..." is
        // the obvious thing to do.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        QVERIFY2(!c->isConnected(), "precondition: never connected");
        c->disconnectFromBroker();          // the else branch — no Paho callback follows
        QVERIFY2(!c->m_reconnectTimer.isActive(), "an explicit disconnect must not retry");

        // Now a real broker-initiated drop, exactly as Paho would deliver it.
        c->onInternalDisconnected();

        QVERIFY2(c->m_reconnectTimer.isActive(),
                 "a genuine broker drop after a no-op disconnect must still re-arm — "
                 "this is the terminal death the slow-retry change exists to remove");
    }

    void userDisconnectIsConsumedExactlyOnce() {
        // The flag must suppress the disconnect the user asked for and nothing after it.
        //
        // Set directly rather than via disconnectFromBroker(): that only arms the flag on
        // the `m_client && m_connected` branch, and m_client is null here because there is
        // no broker to have created one. Faking a non-null m_client would hand a garbage
        // pointer to MQTTAsync_disconnect(). So this covers the CONSUMER's contract, and
        // disconnectWhileNotConnectedDoesNotStrandTheNextRealDrop above covers the
        // producer's — together they pin both halves of the bug.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_userRequestedDisconnect = true;
        c->onInternalDisconnected();        // the callback the user's disconnect caused
        QVERIFY2(!c->m_reconnectTimer.isActive(), "user disconnect must not re-arm");
        QCOMPARE(c->status(), QStringLiteral("Disconnected"));

        c->onInternalDisconnected();        // a later, genuine drop
        QVERIFY2(c->m_reconnectTimer.isActive(), "flag must be one-shot, not sticky");
    }

    void reconnectingClearsAStrandedUserDisconnectFlag() {
        // Second, independent guard: onSettingsChanged() disconnects then immediately
        // reconnects, and connectWithHost() destroys the client whose disconnect callback
        // is still in flight (opts.onFailure is never set, so Paho's teardown loses it).
        // connectToBroker() clears the flag unconditionally so that cannot strand it.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        // Set directly for the same reason as above — and note this test was vacuous
        // when it went through disconnectFromBroker(): with m_client null the flag was
        // never armed, so the "must be cleared" assertion passed without testing anything.
        c->m_userRequestedDisconnect = true;   // as if the callback never arrived
        c->connectToBroker();                  // must clear it
        QVERIFY2(!c->m_userRequestedDisconnect,
                 "a new connect attempt means the prior user-disconnect is meaningless");

        c->onInternalDisconnected();
        QVERIFY2(c->m_reconnectTimer.isActive(),
                 "a drop after reconnecting must re-arm, not read as user-requested");
    }

    // ===== Backoff cadence =====

    void backoffWalksUpThenSettlesOnTheSlowCadence() {
        // The point of the change: the fast budget stops being terminal. After it is
        // spent the client keeps trying forever, just rarely.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        QList<int> intervals;
        for (int i = 0; i < MqttClient::MAX_FAST_RECONNECT_ATTEMPTS; ++i) {
            c->m_reconnectAttempts = i;
            intervals << c->reconnectDelayMs();
        }
        // Monotonic non-decreasing, capped.
        for (int i = 1; i < intervals.size(); ++i)
            QVERIFY2(intervals[i] >= intervals[i - 1], "backoff must not go backwards");
        QCOMPARE(intervals.first(), MqttClient::INITIAL_RECONNECT_DELAY_MS);
        QVERIFY(intervals.last() <= MqttClient::MAX_RECONNECT_DELAY_MS);

        // Past the budget: the slow cadence, forever.
        c->m_reconnectAttempts = MqttClient::MAX_FAST_RECONNECT_ATTEMPTS;
        QCOMPARE(c->reconnectDelayMs(), MqttClient::IDLE_RECONNECT_DELAY_MS);
        c->m_reconnectAttempts = MqttClient::MAX_FAST_RECONNECT_ATTEMPTS + 500;
        QCOMPARE(c->reconnectDelayMs(), MqttClient::IDLE_RECONNECT_DELAY_MS);
    }

    void slowCadenceIsAnnouncedOnceNotEveryCycle() {
        // A warning every 15 minutes forever is log spam that hides real faults; none at
        // all leaves a silent 15-minute-cadence loop nobody can see. Exactly one.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_reconnectAttempts = MqttClient::MAX_FAST_RECONNECT_ATTEMPTS;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("broker unreachable after"));
        c->scheduleReconnect(QStringLiteral("test"));

        // A second pass must be silent — init()'s failOnWarning() is the assertion.
        c->scheduleReconnect(QStringLiteral("test"));
        QVERIFY(c->m_reconnectTimer.isActive());
    }

    // ===== Network reachability =====

    void networkDownSuspendsRetriesAndComingBackRefundsTheBudget() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_reconnectAttempts = 4;
        c->scheduleReconnect(QStringLiteral("test"));
        QVERIFY(c->m_reconnectTimer.isActive());

        c->onNetworkReachabilityChanged(false);
        QVERIFY2(!c->m_reconnectTimer.isActive(), "no point dialling a down interface");
        QCOMPARE(c->status(), QStringLiteral("Waiting for network..."));

        c->onNetworkReachabilityChanged(true);
        QCOMPARE(c->m_reconnectAttempts, 0);   // a regained network gets a fresh budget
        QVERIFY2(c->status() != QStringLiteral("Waiting for network..."),
                 "the waiting status describes a condition that has ended");
    }

    void aDeferredTickDoesNotSpendAnAttempt() {
        // The whole reason reachability is watched: an outage must not burn the fast
        // budget, because an attempt against a down interface says nothing about the
        // broker. Observed on-device 2026-07-25.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_networkDown = true;
        const int before = c->m_reconnectAttempts;
        c->onReconnectTimerTick();
        QCOMPARE(c->m_reconnectAttempts, before);
    }

    void connectedNetworkUnreachableStatusDoesNotLatch() {
        // A reachability blip the TCP session survives (well inside the 60 s keepalive)
        // used to leave "Connected - network unreachable" on the Home Automation tab
        // indefinitely: the resume path cleared only the waiting-for-network string and
        // then early-returned on isConnected(), so nothing ever rewrote it.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_connected = true;
        c->onNetworkReachabilityChanged(false);
        QCOMPARE(c->status(), QStringLiteral("Connected - network unreachable"));

        c->onNetworkReachabilityChanged(true);
        QVERIFY2(c->status() != QStringLiteral("Connected - network unreachable"),
                 "status must not outlive the condition it describes");
    }

    // ===== Enable/disable =====

    void disablingMqttStopsTheLoopAndSaysSo() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->scheduleReconnect(QStringLiteral("test"));
        QVERIFY(c->m_reconnectTimer.isActive());

        settings.mqtt()->setMqttEnabled(false);   // fires onSettingsChanged()
        QVERIFY2(!c->m_reconnectTimer.isActive(), "a disabled feature must not retry");
        QCOMPARE(c->status(), QStringLiteral("Disabled"));
    }

    void failureWhileDisabledIsReportedRatherThanSwallowed() {
        // scheduleReconnect() returns early when MQTT is off — correctly, it must not
        // retry. But the three synchronous connectWithHost() exits lost their own
        // "Error: …" status when they were consolidated here, so returning silently left
        // the tab reading "Connecting..." forever with the Paho rc discarded. A connect
        // CAN be initiated while disabled: the Connect button gates only on host-non-empty.
        Settings settings;
        settings.mqtt()->setMqttEnabled(false);
        settings.mqtt()->setMqttBrokerHost(QStringLiteral("192.0.2.1"));
        QScopedPointer<MqttClient> c(makeClient(settings));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("connect attempt failed while MQTT is disabled"));
        c->scheduleReconnect(QStringLiteral("Connect failed (-3)"));

        QVERIFY2(!c->m_reconnectTimer.isActive(), "still must not retry while disabled");
        QVERIFY2(c->status().contains(QStringLiteral("Connect failed (-3)")),
                 "the broker's own reason must survive to the UI, not be discarded");
    }

    // ===== Status text =====

    void brokerReasonSurvivesIntoTheStatus() {
        // "Bad user name or password" is the whole diagnosis; a bare
        // "reconnecting (3/10)..." that overwrites it makes the fault unfindable.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Connection failed"));
        c->onInternalConnectionFailed(QStringLiteral("Bad user name or password"));

        QVERIFY2(c->status().contains(QStringLiteral("Bad user name or password")),
                 "the broker's reason must reach the Home Automation tab");
        QVERIFY(c->m_reconnectTimer.isActive());
    }
};

QTEST_MAIN(tst_MqttClient)
#include "tst_mqttclient.moc"
