#include <QtTest>
#include <algorithm>
#include <QSignalSpy>
#include <QJsonArray>
#include <QSslError>
#include <QtMqtt/qmqttclient.h>

#include "ble/de1device.h"
#include "core/appsettings.h"
#include "core/settings.h"
#include "core/settings_mqtt.h"
#include "machine/machinestate.h"
#include "network/mqttclient.h"

// MqttClient's own logic: the reconnect state machine, status text, Home Assistant
// discovery, remote stop gating and shot-event bookkeeping.
//
// Why this file exists: mqttclient.cpp was in NO test target, and a change whose entire
// purpose was to stop MQTT reconnection dying permanently shipped a latched
// m_userRequestedDisconnect that reintroduced exactly that death.
//
// Everything here drives private members through the DECENZA_TESTING friend declaration.
// No broker and no waiting: QMqttClient's setState()/setError() are public slots
// (qmqttclient.h:173-180), so the client's reaction to a broker outcome can be driven
// directly.
class tst_MqttClient : public QObject {
    Q_OBJECT

private:
    static MqttClient* makeClient(Settings& settings, DE1Device* device = nullptr,
                                  MachineState* state = nullptr) {
        return new MqttClient(device, state, &settings, settings.mqtt());
    }

    static void enableMqtt(Settings& settings) {
        settings.mqtt()->setMqttEnabled(true);
        settings.mqtt()->setMqttBrokerHost(QStringLiteral("192.0.2.1"));  // TEST-NET-1
    }

    static QJsonObject configFor(MqttClient* c, const QString& objectId) {
        for (const auto& entry : c->discoveryEntries()) {
            if (entry.objectId == objectId)
                return c->componentConfig(entry);
        }
        return {};
    }

    // The test store is shared by every function in this process; identity and discovery
    // bookkeeping would otherwise leak from one test into the next.
    static void clearMqttIdentity() {
        AppSettings raw;
        for (const char* key : {"mqtt/clientId", "mqtt/deviceId", "mqtt/discoveryMigrated",
                                "mqtt/publishedDiscoveryComponents", "mqtt/publishedDiscoveryTopics",
                                "mqtt/homeAssistantDiscovery"})
            raw.remove(QString::fromLatin1(key));
    }

private slots:
    void init() { QTest::failOnWarning(); clearMqttIdentity(); }

    // ===== The regression this file was created for =====

    void disconnectWhileNotConnectedDoesNotStrandTheNextRealDrop() {
        // disconnectFromBroker() used to arm m_userRequestedDisconnect even when no
        // callback would consume it, so the next GENUINE drop was read as user-requested
        // and nothing re-armed — MQTT dead until app restart.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        QVERIFY2(!c->isConnected(), "precondition: never connected");
        c->disconnectFromBroker();          // client Disconnected — no callback follows
        QVERIFY2(!c->m_reconnectTimer.isActive(), "an explicit disconnect must not retry");

        c->onSessionDown();                 // a real broker-initiated drop
        QVERIFY2(c->m_reconnectTimer.isActive(),
                 "a genuine broker drop after a no-op disconnect must still re-arm");
    }

    void userDisconnectIsConsumedExactlyOnce() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_userRequestedDisconnect = true;
        c->onSessionDown();                 // the drop the user's disconnect caused
        QVERIFY2(!c->m_reconnectTimer.isActive(), "user disconnect must not re-arm");
        QCOMPARE(c->status(), QStringLiteral("Disconnected"));

        c->onSessionDown();                 // a later, genuine drop
        QVERIFY2(c->m_reconnectTimer.isActive(), "flag must be one-shot, not sticky");
    }

    void reconnectingClearsAStrandedUserDisconnectFlag() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_userRequestedDisconnect = true;   // as if its callback never arrived
        c->connectToBroker();                  // must clear it
        QVERIFY2(!c->m_userRequestedDisconnect,
                 "a new connect attempt means the prior user-disconnect is meaningless");

        c->onSessionDown();
        QVERIFY2(c->m_reconnectTimer.isActive(),
                 "a drop after reconnecting must re-arm, not read as user-requested");
    }

    // ===== Backoff cadence =====

    void backoffWalksUpThenSettlesOnTheSlowCadence() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        QList<int> intervals;
        for (int i = 0; i < MqttClient::MAX_FAST_RECONNECT_ATTEMPTS; ++i) {
            c->m_reconnectAttempts = i;
            intervals << c->reconnectDelayMs();
        }
        for (int i = 1; i < intervals.size(); ++i)
            QVERIFY2(intervals[i] >= intervals[i - 1], "backoff must not go backwards");
        QCOMPARE(intervals.first(), MqttClient::INITIAL_RECONNECT_DELAY_MS);
        QVERIFY(intervals.last() <= MqttClient::MAX_RECONNECT_DELAY_MS);

        c->m_reconnectAttempts = MqttClient::MAX_FAST_RECONNECT_ATTEMPTS;
        QCOMPARE(c->reconnectDelayMs(), MqttClient::IDLE_RECONNECT_DELAY_MS);
        c->m_reconnectAttempts = MqttClient::MAX_FAST_RECONNECT_ATTEMPTS + 500;
        QCOMPARE(c->reconnectDelayMs(), MqttClient::IDLE_RECONNECT_DELAY_MS);
    }

    void slowCadenceIsAnnouncedOnceNotEveryCycle() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_reconnectAttempts = MqttClient::MAX_FAST_RECONNECT_ATTEMPTS;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("broker unreachable after"));
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
        QCOMPARE(c->m_reconnectAttempts, 0);
        QVERIFY2(c->status() != QStringLiteral("Waiting for network..."),
                 "the waiting status describes a condition that has ended");
    }

    void aDeferredTickDoesNotSpendAnAttempt() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_networkDown = true;
        const int before = c->m_reconnectAttempts;
        c->onReconnectTimerTick();
        QCOMPARE(c->m_reconnectAttempts, before);
    }

    void connectedNetworkUnreachableStatusDoesNotLatch() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_sessionUp = true;
        c->m_connected = true;
        c->onNetworkReachabilityChanged(false);
        QCOMPARE(c->status(), QStringLiteral("Connected - network unreachable"));

        c->onNetworkReachabilityChanged(true);
        QCOMPARE(c->status(), QStringLiteral("Connected"));
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
        Settings settings;
        settings.mqtt()->setMqttEnabled(false);
        settings.mqtt()->setMqttBrokerHost(QStringLiteral("192.0.2.1"));
        QScopedPointer<MqttClient> c(makeClient(settings));

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("connect attempt failed while MQTT is disabled"));
        c->scheduleReconnect(QStringLiteral("connection refused"));

        QVERIFY2(!c->m_reconnectTimer.isActive(), "still must not retry while disabled");
        QVERIFY2(c->status().contains(QStringLiteral("connection refused")),
                 "the reason must survive to the UI, not be discarded");
    }

    // ===== Status text =====

    void brokerReasonSurvivesIntoTheStatus() {
        // "bad username or password" is the whole diagnosis; a bare
        // "reconnecting (3/10)..." that overwrites it makes the fault unfindable.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Connection failed"));
        c->onConnectionFailed(QStringLiteral("bad username or password"));

        QVERIFY(c->status().contains(QStringLiteral("bad username or password")));
        QVERIFY(c->m_reconnectTimer.isActive());
    }

    void failureReasonIsWordsNeverABareCode_data() {
        QTest::addColumn<int>("clientError");
        QTest::addColumn<int>("socketError");
        QTest::addColumn<bool>("sslFailure");
        QTest::addColumn<QString>("expected");

        const int none = QAbstractSocket::UnknownSocketError;
        QTest::newRow("connack 1") << int(QMqttClient::InvalidProtocolVersion) << none << false << QStringLiteral("protocol version");
        QTest::newRow("connack 2") << int(QMqttClient::IdRejected) << none << false << QStringLiteral("client ID");
        QTest::newRow("connack 3") << int(QMqttClient::ServerUnavailable) << none << false << QStringLiteral("broker unavailable");
        QTest::newRow("connack 4") << int(QMqttClient::BadUsernameOrPassword) << none << false << QStringLiteral("bad username or password");
        QTest::newRow("connack 5") << int(QMqttClient::NotAuthorized) << none << false << QStringLiteral("not authorized");
        QTest::newRow("unreachable") << int(QMqttClient::NoError) << int(QAbstractSocket::NetworkError) << false << QStringLiteral("unreachable");
        QTest::newRow("refused") << int(QMqttClient::NoError) << int(QAbstractSocket::ConnectionRefusedError) << false << QStringLiteral("refused");
        QTest::newRow("timeout") << int(QMqttClient::NoError) << int(QAbstractSocket::SocketTimeoutError) << false << QStringLiteral("timed out");
        QTest::newRow("no dns") << int(QMqttClient::NoError) << int(QAbstractSocket::HostNotFoundError) << false << QStringLiteral("not resolved");
        QTest::newRow("cert") << int(QMqttClient::NoError) << int(QAbstractSocket::SslHandshakeFailedError) << true << QStringLiteral("certificate rejected");
        // An error with no dedicated wording still names itself.
        QTest::newRow("other") << int(QMqttClient::NoError) << int(QAbstractSocket::ProxyNotFoundError) << false << QStringLiteral("ProxyNotFoundError");
    }

    void failureReasonIsWordsNeverABareCode() {
        QFETCH(int, clientError);
        QFETCH(int, socketError);
        QFETCH(bool, sslFailure);
        QFETCH(QString, expected);

        QList<QSslError> ssl;
        if (sslFailure)
            ssl << QSslError(QSslError::SelfSignedCertificate);
        const QString reason = MqttClient::failureReason(
            clientError, static_cast<QAbstractSocket::SocketError>(socketError), ssl);

        QVERIFY2(reason.contains(expected, Qt::CaseInsensitive), qPrintable(reason));
        QVERIFY2(!QRegularExpression(QStringLiteral("^\\(?-?\\d+\\)?$")).match(reason).hasMatch(),
                 "a bare number is not a reason");
    }

    void refusedSubscriptionIsNamedAndNotConnected() {
        // An account that may not read the command topic must not look healthy: every
        // Home Assistant command would silently go nowhere.
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->m_sessionUp = true;
        c->m_connected = true;
        c->m_status = QStringLiteral("Connected");
        // ShotServer's connect endpoint reads isConnected() inside statusChanged.
        bool connectedSeenByStatusHandler = true;
        connect(c.data(), &MqttClient::statusChanged, c.data(),
                [&]() { connectedSeenByStatusHandler = c->isConnected(); });

        c->m_refusedSubscription = QStringLiteral("decenza/command");
        c->updateVerifiedState();

        QVERIFY(!c->isConnected());
        QVERIFY2(!connectedSeenByStatusHandler, "statusChanged fired before isConnected() was updated");
        QVERIFY(c->status().contains(QStringLiteral("decenza/command")));
        QVERIFY(c->status().contains(QStringLiteral("permission")));
    }

    void brokerRejectingMqtt311FallsBackTo31WithoutSpendingAnAttempt() {
        Settings settings;
        enableMqtt(settings);
        QScopedPointer<MqttClient> c(makeClient(settings));
        c->m_attemptAddress = QStringLiteral("192.0.2.1");
        c->m_reconnectAttempts = 3;

        c->m_client->setState(QMqttClient::Connecting);
        c->m_client->setError(QMqttClient::InvalidProtocolVersion);
        c->m_client->setState(QMqttClient::Disconnected);

        QVERIFY(c->m_useMqtt31);
        QCOMPARE(c->m_reconnectAttempts, 3);
        QVERIFY2(!c->status().startsWith(QStringLiteral("Error")), "a fallback is not a failure");

        // A second rejection, already on 3.1, is a real failure.
        c->m_client->setState(QMqttClient::Connecting);
        c->m_client->setError(QMqttClient::NoError);
        c->m_client->setError(QMqttClient::InvalidProtocolVersion);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("protocol version"));
        c->m_client->setState(QMqttClient::Disconnected);
        QVERIFY(c->status().contains(QStringLiteral("protocol version")));
    }

    void changesThatWouldExposeTheStoredPasswordAreFlagged() {
        // The web page asks for the password again before these, and MCP refuses them:
        // either would otherwise hand the stored password to a broker nobody vetted, or
        // send it in the clear.
        Settings settings;
        SettingsMqtt* m = settings.mqtt();
        m->setMqttBrokerHost(QStringLiteral("broker.lan"));
        m->setMqttUseTls(true);

        const QJsonObject risky{{"mqttBrokerHost", "evil.lan"}, {"mqttBrokerPort", 1884},
                                {"mqttUseTls", false}, {"mqttCaCertificate", "x"}};
        QVERIFY2(m->passwordExposingChanges(risky).isEmpty(), "no stored password, nothing to expose");

        m->setMqttPassword(QStringLiteral("secret"));
        QCOMPARE(m->passwordExposingChanges(risky).size(), 4);

        // Same values, or turning TLS ON, expose nothing.
        const QJsonObject harmless{{"mqttBrokerHost", "broker.lan"}, {"mqttBrokerPort", m->mqttBrokerPort()},
                                   {"mqttUseTls", true}, {"mqttBaseTopic", "elsewhere"}};
        QVERIFY(m->passwordExposingChanges(harmless).isEmpty());
    }

    // ===== Home Assistant discovery =====

    void existingEntitiesKeepTheirTopicsAndIds() {
        // Upgrading must not create a second set of entities in Home Assistant: every
        // config topic and unique_id shipped before the Qt MQTT move stays exactly as was.
        Settings settings;
        settings.mqtt()->importMqttDeviceId(QStringLiteral("cid"));
        QScopedPointer<MqttClient> c(makeClient(settings));

        const QList<std::tuple<QString, QString, QString>> shipped = {
            {"sensor", "state", "state"}, {"sensor", "phase", "phase"}, {"sensor", "substate", "substate"},
            {"sensor", "temperature_head", "temp_head"}, {"sensor", "temperature_mix", "temp_mix"},
            {"sensor", "temperature_steam", "temp_steam"}, {"sensor", "pressure", "pressure"},
            {"sensor", "flow", "flow"}, {"sensor", "weight", "weight"},
            {"sensor", "target_weight", "target_weight"}, {"sensor", "water_level", "water_level"},
            {"sensor", "water_level_ml", "water_level_ml"}, {"sensor", "shot_time", "shot_time"},
            {"text", "profile", "profile"}, {"switch", "power", "power"},
            {"binary_sensor", "connected", "connected"}, {"binary_sensor", "scale_connected", "scale_connected"},
            {"sensor", "steam_mode", "steam_mode"}, {"switch", "steam", "steam"},
            {"sensor", "espresso_count", "espresso_count"}, {"sensor", "profile_filename", "profile_filename"},
        };
        const auto entries = c->discoveryEntries();
        for (const auto& [component, objectId, suffix] : shipped) {
            bool found = false;
            for (const auto& entry : entries) {
                if (entry.component == component && entry.objectId == objectId) {
                    found = true;
                    QCOMPARE(c->componentConfig(entry).value("unique_id").toString(),
                             QStringLiteral("de1_cid_") + suffix);
                }
            }
            QVERIFY2(found, qPrintable(component + "/" + objectId + " missing"));
        }
    }

    void entityAvailabilityFollowsTheDeviceItReads() {
        Settings settings;
        QScopedPointer<MqttClient> c(makeClient(settings));

        auto deviceTopicOf = [](const QJsonObject& config) {
            const QJsonArray list = config.value("availability").toArray();
            return list.size() == 2 ? list.at(1).toObject().value("topic").toString() : QString();
        };

        const QJsonObject temp = configFor(c.data(), "temperature_head");
        QCOMPARE(deviceTopicOf(temp), QStringLiteral("decenza/connected"));
        QCOMPARE(temp.value("availability_mode").toString(), QStringLiteral("all"));

        QCOMPARE(deviceTopicOf(configFor(c.data(), "weight")), QStringLiteral("decenza/scale_connected"));

        // The connection entities themselves must stay readable while the device is gone.
        const QJsonObject link = configFor(c.data(), "connected");
        QVERIFY(!link.contains("availability"));
        QCOMPARE(link.value("availability_topic").toString(), QStringLiteral("decenza/availability"));
    }

    void profileSelectListsTheInstalledTitles() {
        Settings settings;
        QScopedPointer<MqttClient> c(makeClient(settings));

        QVERIFY2(configFor(c.data(), "profile_select").isEmpty(),
                 "Home Assistant rejects a select with no options");

        c->setProfileTitles({QStringLiteral("D-Flow / Q"), QStringLiteral("Londinium")});
        const QJsonObject select = configFor(c.data(), "profile_select");
        QCOMPARE(select.value("options").toArray().size(), 2);
        QCOMPARE(select.value("command_topic").toString(), QStringLiteral("decenza/profile/select"));

        // The older text entity stays for existing users but is not offered to new ones.
        const QJsonObject text = configFor(c.data(), "profile");
        QVERIFY(text.contains("enabled_by_default"));
        QVERIFY(!text.value("enabled_by_default").toBool(true));
    }

    void upgradeKeepsTheHomeAssistantIdentityAndRenewsTheClientId() {
        // The old client ID built every unique_id, so it becomes the device ID; the client
        // ID itself is renewed, which also separates installs a backup gave the same ID.
        Settings settings;
        settings.mqtt()->setMqttClientId(QStringLiteral("decenza_localhost_45d999b4"));
        QScopedPointer<MqttClient> c(makeClient(settings));

        QCOMPARE(settings.mqtt()->mqttDeviceId(), QStringLiteral("decenza_localhost_45d999b4"));
        QVERIFY(settings.mqtt()->mqttClientId() != QStringLiteral("decenza_localhost_45d999b4"));
        QVERIFY2(!settings.mqtt()->mqttDiscoveryMigrated(), "an upgraded install has per-entity topics to move");
        QCOMPARE(configFor(c.data(), "temperature_head").value("unique_id").toString(),
                 QStringLiteral("de1_decenza_localhost_45d999b4_temp_head"));
    }

    void devicePayloadFollowsHomeAssistantsShape() {
        Settings settings;
        QScopedPointer<MqttClient> c(makeClient(settings));
        c->setProfileTitles({QStringLiteral("D-Flow / Q")});

        const QJsonObject payload = c->deviceDiscoveryPayload();
        QVERIFY(payload.value("device").toObject().contains("identifiers"));
        QVERIFY2(payload.value("origin").toObject().contains("name"), "origin is mandatory for device discovery");
        const QJsonObject components = payload.value("components").toObject();
        QCOMPARE(components.size(), c->discoveryEntries().size());
        for (auto it = components.begin(); it != components.end(); ++it) {
            const QJsonObject component = it.value().toObject();
            QVERIFY2(component.contains("platform") && component.contains("unique_id"), qPrintable(it.key()));
            QVERIFY2(!component.contains("device"), "device belongs at the root, once");
        }
        QVERIFY(c->deviceDiscoveryTopic().startsWith(QStringLiteral("homeassistant/device/decenza_")));
    }

    void migrationFollowsHomeAssistantsOrder() {
        Settings settings;
        settings.mqtt()->setMqttClientId(QStringLiteral("old_client"));   // an upgraded install
        settings.mqtt()->setMqttHomeAssistantDiscovery(true);
        QScopedPointer<MqttClient> c(makeClient(settings));
        QList<MqttClient::Published> sent;
        c->m_publishRecorder = &sent;

        c->publishHomeAssistantDiscovery();

        const QStringList legacy = MqttClient::legacyDiscoveryTopics();
        QCOMPARE(sent.size(), legacy.size() * 2 + 1);
        for (int i = 0; i < legacy.size(); ++i) {
            QCOMPARE(sent[i].topic, legacy[i]);
            QVERIFY(sent[i].payload.contains(QStringLiteral("migrate_discovery")));
            QVERIFY2(!sent[i].retain, "the migrate message is not retained");
        }
        QCOMPARE(sent[legacy.size()].topic, c->deviceDiscoveryTopic());
        for (int i = 0; i < legacy.size(); ++i) {
            const auto& clear = sent[legacy.size() + 1 + i];
            QCOMPARE(clear.topic, legacy[i]);
            QVERIFY(clear.payload.isEmpty() && clear.retain);
        }
        QVERIFY(settings.mqtt()->mqttDiscoveryMigrated());

        sent.clear();
        c->publishHomeAssistantDiscovery();
        QCOMPARE(sent.size(), 1);   // once migrated, only the device message
    }

    void aComponentThatLeavesIsSentOnceWithOnlyItsPlatform() {
        Settings settings;
        QScopedPointer<MqttClient> c(makeClient(settings));
        QList<MqttClient::Published> sent;
        c->m_publishRecorder = &sent;

        c->setProfileTitles({QStringLiteral("Londinium")});
        c->publishHomeAssistantDiscovery();
        c->setProfileTitles({});

        sent.clear();
        c->publishHomeAssistantDiscovery();
        QJsonObject select = QJsonDocument::fromJson(sent.last().payload.toUtf8())
                                 .object().value("components").toObject().value("profile_select").toObject();
        QCOMPARE(select, (QJsonObject{{"platform", "select"}}));

        sent.clear();
        c->publishHomeAssistantDiscovery();
        QVERIFY(!QJsonDocument::fromJson(sent.last().payload.toUtf8())
                     .object().value("components").toObject().contains("profile_select"));
    }

    void newDeviceIdLeavesThePreviousIdentityAlone() {
        // Its use is a second install restored from the same backup: the previous identity
        // is the other install's, so nothing published under it may be cleared or migrated.
        Settings settings;
        settings.mqtt()->setMqttClientId(QStringLiteral("shared_with_the_tablet"));
        settings.mqtt()->setMqttHomeAssistantDiscovery(true);
        QScopedPointer<MqttClient> c(makeClient(settings));
        const QString previousTopic = c->deviceDiscoveryTopic();
        QList<MqttClient::Published> sent;
        c->m_publishRecorder = &sent;
        c->m_sessionUp = true;

        c->newDeviceId();

        QVERIFY(settings.mqtt()->mqttDeviceId() != QStringLiteral("shared_with_the_tablet"));
        const QStringList legacy = MqttClient::legacyDiscoveryTopics();
        for (const auto& message : sent) {
            QVERIFY2(message.topic != previousTopic, "the shared device message must not be touched");
            QVERIFY2(!legacy.contains(message.topic), "the shared per-entity topics must not be touched");
        }
        QVERIFY(std::any_of(sent.cbegin(), sent.cend(),
                            [&](const auto& m) { return m.topic == c->deviceDiscoveryTopic(); }));
    }

    void recipeSelectListsRecipesAndSaysNoneWhenInactive() {
        Settings settings;
        QScopedPointer<MqttClient> c(makeClient(settings));
        QVERIFY2(configFor(c.data(), "recipe_select").isEmpty(), "no recipes, no select");

        c->setRecipeTitles({QStringLiteral("Morning Latte"), QStringLiteral("Cortado")});
        const QJsonObject select = configFor(c.data(), "recipe_select");
        QCOMPARE(select.value("options").toArray().size(), 2);
        QCOMPARE(select.value("command_topic").toString(), QStringLiteral("decenza/recipe/select"));

        // An MQTT select has no current option only when told "None".
        QList<MqttClient::Published> sent;
        c->m_publishRecorder = &sent;
        c->setActiveRecipe(QStringLiteral("Cortado"));
        c->setActiveRecipe(QString());
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent[0].payload, QStringLiteral("Cortado"));
        QCOMPARE(sent[1].payload, QStringLiteral("None"));
    }

    // ===== Last shot and shot events =====

    void lastShotOmitsWhatTheShotDoesNotHave() {
        MqttClient::LastShot noScale;
        noScale.startEpochSec = 1790000000;
        noScale.durationSec = 28.04;
        noScale.doseG = 18.0;
        noScale.profile = QStringLiteral("D-Flow / Q");
        const QJsonObject summary = MqttClient::lastShotSummary(noScale);
        QVERIFY2(!summary.contains("yield_g") && !summary.contains("ratio"),
                 "a 0 g yield would read as a measurement");
        QCOMPARE(summary.value("duration_s").toDouble(), 28.0);
        QVERIFY(summary.value("finished_at").toString().contains('T'));

        MqttClient::LastShot full = noScale;
        full.yieldG = 36.0;
        QCOMPARE(MqttClient::lastShotSummary(full).value("ratio").toDouble(), 2.0);
    }

    void eachEspressoGetsAtMostOneOutcomeAndMaintenanceNone() {
        Settings settings;
        QScopedPointer<MqttClient> c(makeClient(settings));

        c->onEspressoCycleStarted(false);
        QVERIFY(c->m_shotCycleOpen);
        c->onShotPersisted(28.0, 36.0);
        QVERIFY2(!c->m_shotCycleOpen, "finished closes the cycle");
        c->onShotNotSaved();   // must not add an aborted to a finished shot
        QVERIFY(!c->m_shotCycleOpen);

        c->onEspressoCycleStarted(true);   // cleaning profile
        QVERIFY2(!c->m_shotCycleOpen, "maintenance runs report nothing");
    }

    // ===== Remote stop =====

    void stopActsOnlyDuringBeverageAndRinsePhases() {
        Settings settings;
        DE1Device device;
        device.m_simulationMode = true;    // isConnected() -> true
        MachineState state(&device);
        QScopedPointer<MqttClient> c(makeClient(settings, &device, &state));
        QSignalSpy stops(c.data(), &MqttClient::stopRequested);

        state.m_phase = MachineState::Phase::Pouring;
        c->handleCommand(QStringLiteral("stop"));
        QCOMPARE(stops.count(), 1);

        for (auto phase : {MachineState::Phase::Cleaning, MachineState::Phase::Descaling,
                           MachineState::Phase::Idle, MachineState::Phase::Sleep}) {
            state.m_phase = phase;
            c->handleCommand(QStringLiteral("stop"));
        }
        QCOMPARE(stops.count(), 1);
    }
};

QTEST_MAIN(tst_MqttClient)
#include "tst_mqttclient.moc"
