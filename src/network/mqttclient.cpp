#include "core/diagnosticlogging.h"
#include "mqttclient.h"

#include <QDateTime>
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../core/settings.h"
#include "../core/settings_brew.h"
#include "../core/settings_mqtt.h"
#include "../controllers/maincontroller.h"
#include "version.h"

#include <QtMqtt/qmqttclient.h>
#include <QtMqtt/qmqttsubscription.h>
#include <QtMqtt/qmqtttopicfilter.h>
#include <QtMqtt/qmqtttopicname.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaEnum>
#include <QNetworkInformation>
#include <QTcpSocket>
#if QT_CONFIG(ssl)
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#endif
#ifdef Q_OS_ANDROID
#include <QThread>
#include "mdnsresolver.h"
#endif
#include "localnetworkaccess.h"

namespace {
constexpr auto kHomeAssistantStatusTopic = "homeassistant/status";
}

MqttClient::MqttClient(DE1Device* device, MachineState* machineState,
                       Settings* settings, SettingsMqtt* settingsMqtt,
                       QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
    , m_settings(settings)
    , m_settingsMqtt(settingsMqtt)
{
    m_client = new QMqttClient(this);
    m_client->setKeepAlive(60);
    m_client->setCleanSession(true);
    connect(m_client, &QMqttClient::stateChanged, this, &MqttClient::onClientStateChanged);
    connect(m_client, &QMqttClient::messageReceived, this,
            [this](const QByteArray& message, const QMqttTopicName& topic) {
                onMessageReceived(message, topic.name());
            });

    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged, this, &MqttClient::onPhaseChanged);
    }
    if (m_device) {
        connect(m_device, &DE1Device::waterLevelChanged, this, &MqttClient::onWaterLevelChanged);
        connect(m_device, &DE1Device::stateChanged, this, &MqttClient::onDE1StateChanged);
        connect(m_device, &DE1Device::subStateChanged, this, &MqttClient::onDE1StateChanged);
        connect(m_device, &DE1Device::connectedChanged, this, &MqttClient::onDE1ConnectedChanged);
    }

    if (m_settingsMqtt) {
        m_settingsMqtt->ensureMqttIdentity();
        connect(m_settingsMqtt, &SettingsMqtt::mqttEnabledChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttBrokerHostChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttBrokerPortChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttUsernameChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttPasswordChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttUseTlsChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttCaCertificateChanged, this, &MqttClient::onSettingsChanged);
        // Both are part of the session itself (CONNECT, the will, the subscriptions), so
        // they only take effect on a new one.
        connect(m_settingsMqtt, &SettingsMqtt::mqttClientIdChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttBaseTopicChanged, this, &MqttClient::onSettingsChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttHomeAssistantDiscoveryChanged,
                this, &MqttClient::onDiscoverySettingChanged);
        connect(m_settingsMqtt, &SettingsMqtt::mqttPublishIntervalChanged, this, [this]() {
            if (m_publishTimer.isActive()) {
                m_publishTimer.setInterval(m_settingsMqtt->mqttPublishInterval());
            }
        });
    }

    connect(&m_publishTimer, &QTimer::timeout, this, &MqttClient::onPublishTimerTick);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &MqttClient::onReconnectTimerTick);

    m_attemptDeadline.setSingleShot(true);
    connect(&m_attemptDeadline, &QTimer::timeout, this, [this]() {
        if (m_pendingSocket) {
            abandonSocket(m_pendingSocket);
            onConnectionFailed(QStringLiteral("timed out"));
        } else if (m_activeSocket && m_client->state() != QMqttClient::Connected) {
            // Socket up, CONNACK never came. Closing it makes Qt MQTT report Disconnected,
            // which lands in onClientStateChanged() — tell it why.
            m_failureOverride = QStringLiteral("timed out waiting for the broker to answer");
            m_activeSocket->abort();
        }
    });

    // The FAST reconnect budget is for "the broker is not answering us", not for "this
    // device has no network" — an attempt against a down interface says nothing about the
    // broker, and spending the budget on an outage burned through it (observed on-device
    // 2026-07-25). So: don't spend attempts while reachability is positively down, and treat
    // its return as a fresh budget. Only "Disconnected" counts as down — Unknown is what a
    // backend reports when it cannot tell (macOS does exactly this at startup).
    if (QNetworkInformation::loadDefaultBackend()) {
        if (auto* info = QNetworkInformation::instance()) {
            m_networkDown = (info->reachability() == QNetworkInformation::Reachability::Disconnected);
            connect(info, &QNetworkInformation::reachabilityChanged, this,
                    [this](QNetworkInformation::Reachability reachability) {
                        onNetworkReachabilityChanged(
                            reachability != QNetworkInformation::Reachability::Disconnected);
                    });
        } else {
            DIAG_INFO(NETWORK, "MqttClient") << "QNetworkInformation backend loaded but no instance - "
                       "reconnect attempts will be spent while offline";
        }
    } else {
        DIAG_INFO(NETWORK, "MqttClient") << "no QNetworkInformation backend - reconnect attempts "
                   "will be spent while offline (pre-existing behaviour)";
    }

    m_status = "Disconnected";
}

MqttClient::~MqttClient()
{
    // The disconnect below reaches onClientStateChanged() synchronously, which would
    // emit and arm a reconnect from inside the destructor.
    disconnect(m_client, nullptr, this, nullptr);
    if (m_sessionUp) {
        // The broker publishes the LWT only for an unclean drop, so a clean exit says
        // "offline" itself. disconnectFromHost() then writes DISCONNECT and flushes both
        // with a blocking waitForBytesWritten(30000) (qmqttconnection.cpp:716-741) — which
        // is only quick when the link is moving. With a write backlog it is not, so drop
        // the connection instead and let the broker's LWT say "offline".
        if (m_activeSocket && m_activeSocket->bytesToWrite() == 0) {
            publishAvailability(false);
            m_client->disconnectFromHost();
        } else if (m_activeSocket) {
            m_activeSocket->abort();
        }
    }
    // Before the sockets, which are children: ~QMqttClient dereferences the transport it
    // was given, sending DISCONNECT if connected and then always disconnect()ing from it
    // (qmqttclient.cpp:339-348).
    delete m_client;
    m_client = nullptr;
}

QString MqttClient::deviceId() const
{
    return m_settingsMqtt ? m_settingsMqtt->mqttDeviceId() : QString();
}

void MqttClient::newDeviceId()
{
    if (!m_settingsMqtt)
        return;
    m_settingsMqtt->regenerateMqttDeviceId();
    DIAG_INFO(NETWORK, "MqttClient") << "New Home Assistant device ID:" << m_settingsMqtt->mqttDeviceId();
    if (m_sessionUp && m_settingsMqtt->mqttHomeAssistantDiscovery()) {
        publishHomeAssistantDiscovery();
        republishAll();
    }
}

QString MqttClient::failureReason(int clientError, QAbstractSocket::SocketError socketError,
                                  const QList<QSslError>& sslErrors)
{
    switch (clientError) {
    case QMqttClient::InvalidProtocolVersion: return QStringLiteral("broker rejected the MQTT protocol version");
    case QMqttClient::IdRejected:             return QStringLiteral("broker rejected this client ID");
    case QMqttClient::ServerUnavailable:      return QStringLiteral("broker unavailable");
    case QMqttClient::BadUsernameOrPassword:  return QStringLiteral("bad username or password");
    case QMqttClient::NotAuthorized:          return QStringLiteral("not authorized");
    case QMqttClient::ProtocolViolation:      return QStringLiteral("broker sent an invalid MQTT response");
    default: break;
    }

    if (!sslErrors.isEmpty())
        return QStringLiteral("certificate rejected: ") + sslErrors.first().errorString();

    switch (socketError) {
    case QAbstractSocket::HostNotFoundError:       return QStringLiteral("broker host name not resolved");
    case QAbstractSocket::ConnectionRefusedError:  return QStringLiteral("connection refused - nothing is listening on that port");
    case QAbstractSocket::SocketTimeoutError:      return QStringLiteral("timed out");
    // Qt maps EHOSTUNREACH/ENETUNREACH here (qnativesocketengine_unix.cpp).
    case QAbstractSocket::NetworkError:            return QStringLiteral("broker host unreachable");
    case QAbstractSocket::RemoteHostClosedError:   return QStringLiteral("broker closed the connection");
    case QAbstractSocket::SslHandshakeFailedError: return QStringLiteral("TLS handshake failed");
    case QAbstractSocket::UnknownSocketError:      return QStringLiteral("connection failed");
    default: break;
    }
    // A name, never a bare number: the status reaches the user verbatim.
    const char* key = QMetaEnum::fromType<QAbstractSocket::SocketError>().valueToKey(socketError);
    return QStringLiteral("connection failed (%1)").arg(key ? QString::fromLatin1(key) : QStringLiteral("unknown"));
}

QString MqttClient::describeCaCertificate(const QString& pem) const
{
    return SettingsMqtt::describeCaCertificate(pem);
}

void MqttClient::connectToBroker()
{
    // A user-requested disconnect only describes the connection it ended; once we are
    // dialling again it is meaningless. Cleared unconditionally so no path can strand it.
    m_userRequestedDisconnect = false;
    [[maybe_unused]] const quint64 generation = ++m_attemptGeneration;   // Android mDNS

    if (!m_settingsMqtt) {
        m_status = "Error: No settings";
        emit statusChanged();
        return;
    }

    const QString host = m_settingsMqtt->mqttBrokerHost().trimmed();
    if (host.isEmpty()) {
        m_status = "Error: No broker host configured";
        emit statusChanged();
        return;
    }
    // A wildcard makes every topic invalid (qmqtttopicname.cpp:100-108): publishes fail
    // and subscriptions never exist, so nothing would ever say why.
    if (!QMqttTopicName(topicPath(QStringLiteral("state"))).isValid()) {
        m_status = "Error: the base topic cannot contain + or #";
        emit statusChanged();
        return;
    }
    LocalNetworkAccess::request(LocalNetworkAccess::Feature::Mqtt);
    m_attemptHost = host;

#ifdef Q_OS_ANDROID
    // Android's getaddrinfo() doesn't reliably resolve .local mDNS hostnames.
    // Resolve on a background thread to avoid blocking the UI.
    if (host.endsWith(".local", Qt::CaseInsensitive)) {
        m_status = "Resolving...";
        emit statusChanged();

        QPointer<MqttClient> guard(this);
        QThread* thread = QThread::create([guard, host, generation]() {
            const QString resolved = MdnsResolver::resolveHostname(host);
            QMetaObject::invokeMethod(guard.data(), [guard, generation, host, resolved]() {
                if (guard)
                    guard->onMdnsResolved(generation, host, resolved);
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
        return;
    }
#endif

    connectWithHost(host);
}

void MqttClient::onMdnsResolved(quint64 generation, const QString& host, const QString& resolved)
{
    // Anything inside the ~2 s resolve can make this answer stale: a newer attempt, a
    // Disconnect, another broker, or a flapping AP whose answer would overwrite
    // "Waiting for network..." and dial a dead interface.
    if (generation != m_attemptGeneration || m_networkDown || !m_settingsMqtt
        || !m_settingsMqtt->mqttEnabled() || host != m_settingsMqtt->mqttBrokerHost().trimmed()) {
        DIAG_DEBUG(NETWORK, "MqttClient") << "mDNS answer for" << host << "is stale - not connecting";
        return;
    }
    if (!resolved.isEmpty()) {
        DIAG_DEBUG(NETWORK, "MqttClient") << "Resolved" << host << "to" << resolved << "via mDNS";
        connectWithHost(resolved);
        return;
    }
    {
        LogCollapse::Collapsed collapsed;
        const QString text = QStringLiteral("mDNS resolution failed for %1 - trying direct connection").arg(host);
        if (m_logCollapse.shouldLog(QStringLiteral("mdns"), text,
                                    QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
            DIAG_WARN(NETWORK, "MqttClient").noquote() << text + m_logCollapse.suffix(collapsed);
        }
    }
    connectWithHost(host);
}

void MqttClient::connectWithHost(const QString& address)
{
    // Close whatever is open: a session still up here is being replaced (settings
    // changed, or Connect pressed again).
    if (m_pendingSocket)
        abandonSocket(m_pendingSocket);
    if (m_client->state() != QMqttClient::Disconnected && m_activeSocket) {
        // Replacing a session is not a fault, and the status belongs to the new attempt.
        // abort() reaches onClientStateChanged() before it returns (QIODevice::aboutToClose
        // -> transportConnectionClosed, qmqttconnection.cpp:139,803-815).
        m_replacingSession = true;
        m_activeSocket->abort();
        m_replacingSession = false;
    }
    // This attempt replaces any scheduled one; a retry left armed would fire later and
    // abort the session this attempt opens, every cycle.
    m_reconnectTimer.stop();

    if (!m_isReconnecting) {
        m_reconnectAttempts = 0;
        m_slowRetryAnnounced = false;
        emit reconnectAttemptsChanged();
    }
    m_isReconnecting = false;
    m_failureOverride.clear();
    m_lastSslErrors.clear();
    m_attemptAddress = address;

    const int port = m_settingsMqtt ? m_settingsMqtt->mqttBrokerPort() : 1883;
    m_attemptTls = m_settingsMqtt && m_settingsMqtt->mqttUseTls();

    m_client->setClientId(m_settingsMqtt->mqttClientId());
    m_client->setUsername(m_settingsMqtt->mqttUsername());
    m_client->setPassword(m_settingsMqtt->mqttPassword());
    m_client->setProtocolVersion(m_useMqtt31 ? QMqttClient::MQTT_3_1 : QMqttClient::MQTT_3_1_1);
    m_client->setWillTopic(topicPath("availability"));
    m_client->setWillMessage("offline");
    m_client->setWillQoS(1);
    m_client->setWillRetain(true);
    m_client->setHostname(address);
    m_client->setPort(static_cast<quint16>(port));

    QTcpSocket* socket = nullptr;
#if QT_CONFIG(ssl)
    if (m_attemptTls) {
        auto* ssl = new QSslSocket(this);
        QSslConfiguration config = QSslConfiguration::defaultConfiguration();
        const QString pem = m_settingsMqtt->mqttCaCertificate().trimmed();
        if (!pem.isEmpty()) {
            const QList<QSslCertificate> extra = QSslCertificate::fromData(pem.toUtf8(), QSsl::Pem);
            if (extra.isEmpty()) {
                DIAG_WARN(NETWORK, "MqttClient") << "CA certificate setting is not a readable PEM certificate - ignoring it";
            } else {
                // From the full system store: setting CAs turns off on-demand root loading
                // (qsslconfiguration.cpp:650-654), and where that is on (Linux OpenSSL)
                // caCertificates() holds none, so public brokers would stop verifying.
                config.setCaCertificates(QSslConfiguration::systemCaCertificates() + extra);
            }
        }
        config.setPeerVerifyMode(QSslSocket::VerifyPeer);
        ssl->setSslConfiguration(config);
        connect(ssl, &QSslSocket::sslErrors, this, [this, ssl](const QList<QSslError>& errors) {
            if (ssl == m_pendingSocket)
                m_lastSslErrors = errors;
        });
        connect(ssl, &QSslSocket::encrypted, this, [this, ssl]() { onSocketReady(ssl); });
        socket = ssl;
    }
#endif
    if (!socket) {
        socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, [this, socket]() { onSocketReady(socket); });
    }
    // Queued: connectToHost() with an IP literal can emit errorOccurred synchronously
    // (qnativesocketengine_unix.cpp, qabstractsocket.cpp), i.e. before we return.
    connect(socket, &QAbstractSocket::errorOccurred, this,
            [this, guard = QPointer<QTcpSocket>(socket)](QAbstractSocket::SocketError) {
                if (guard)
                    onSocketError(guard);
            }, Qt::QueuedConnection);
    m_pendingSocket = socket;

    m_status = kConnecting;
    emit statusChanged();

    {
        LogCollapse::Collapsed collapsed;
        const QString text = QStringLiteral("Connecting to %1://%2:%3")
                                 .arg(m_attemptTls ? QStringLiteral("ssl") : QStringLiteral("tcp"),
                                      address).arg(port);
        if (m_logCollapse.shouldLog(QStringLiteral("connecting"), text,
                                    QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
            DIAG_DEBUG(NETWORK, "MqttClient").noquote() << text + m_logCollapse.suffix(collapsed);
        }
    }

    m_attemptDeadline.start(ATTEMPT_DEADLINE_MS);
#if QT_CONFIG(ssl)
    if (m_attemptTls) {
        // The certificate is checked against the name the user entered, even when
        // `address` came from our own mDNS lookup.
        static_cast<QSslSocket*>(socket)->connectToHostEncrypted(address, static_cast<quint16>(port), m_attemptHost);
        return;
    }
#endif
    socket->connectToHost(address, static_cast<quint16>(port));
}

void MqttClient::onSocketReady(QTcpSocket* socket)
{
    if (socket != m_pendingSocket)
        return;
    m_pendingSocket = nullptr;

    // setTransport() lets go of the previous socket by pointer, so it is deleted only now.
    QPointer<QTcpSocket> previous = m_activeSocket;
    m_activeSocket = socket;

    // After the hand-over a remote close emits disconnected() but not aboutToClose(),
    // and setTransport() listens only to aboutToClose/readyRead (qmqttconnection.cpp:128-142).
    // close() is what tells Qt MQTT the session is gone.
    disconnect(socket, nullptr, this, nullptr);
    connect(socket, &QAbstractSocket::disconnected, socket, [socket]() { socket->close(); });
    connect(socket, &QAbstractSocket::errorOccurred, socket, [socket]() { socket->close(); });

    m_client->setTransport(socket, m_attemptTls ? QMqttClient::SecureSocket : QMqttClient::AbstractSocket);
    if (previous && previous != socket)
        previous->deleteLater();
    // The transport is already connected, so this only sends CONNECT
    // (qmqttconnection.cpp:271-308).
    m_client->connectToHost();
}

void MqttClient::onSocketError(QTcpSocket* socket)
{
    if (socket != m_pendingSocket)
        return;   // After the hand-over, Qt MQTT owns the outcome.
    m_attemptDeadline.stop();
    const QString reason = failureReason(QMqttClient::NoError, socket->error(), m_lastSslErrors);
    abandonSocket(socket);
    onConnectionFailed(reason);
}

void MqttClient::abandonSocket(QTcpSocket* socket)
{
    if (!socket)
        return;
    disconnect(socket, nullptr, this, nullptr);
    socket->abort();
    socket->deleteLater();
    if (m_pendingSocket == socket)
        m_pendingSocket = nullptr;
}

void MqttClient::onClientStateChanged()
{
    const QMqttClient::ClientState state = m_client->state();
    if (state == QMqttClient::Connected) {
        m_attemptDeadline.stop();
        onSessionUp();
        return;
    }
    if (state != QMqttClient::Disconnected)
        return;

    if (m_replacingSession) {
        m_attemptDeadline.stop();
        endSession();
        return;
    }

    if (m_sessionUp) {
        onSessionDown();
        return;
    }

    // The handshake ended before CONNACK accepted us. Qt MQTT sets the error before the
    // state (qmqttclient.cpp:1103-1110), so error() already holds the reason.
    m_attemptDeadline.stop();
    if (m_userRequestedDisconnect) {
        m_userRequestedDisconnect = false;
        m_status = "Disconnected";
        emit statusChanged();
        return;
    }

    const QMqttClient::ClientError error = m_client->error();
    if (error == QMqttClient::InvalidProtocolVersion && !m_useMqtt31) {
        // A 3.1-only broker refuses a 3.1.1 CONNECT: retry once with 3.1. Not counted
        // against the retry budget, and queued — we are inside Qt MQTT's closeConnection()
        // (qmqttconnection.cpp:862-873).
        m_useMqtt31 = true;
        DIAG_INFO(NETWORK, "MqttClient") << "broker rejected MQTT 3.1.1 - retrying with MQTT 3.1";
        QMetaObject::invokeMethod(this, [this]() {
            m_isReconnecting = true;
            connectWithHost(m_attemptAddress);
        }, Qt::QueuedConnection);
        return;
    }

    QString reason = m_failureOverride;
    if (reason.isEmpty()) {
        const QAbstractSocket::SocketError socketError =
            m_activeSocket ? m_activeSocket->error() : QAbstractSocket::UnknownSocketError;
        reason = failureReason(error, socketError, m_lastSslErrors);
    }
    onConnectionFailed(reason);
}

void MqttClient::onSessionUp()
{
    // Close the books on the outage, if there was one: the collapsed ladders stop being
    // called now, so nothing else would flush their counts, and the recovery itself must
    // be visible at INFO — an outage announced at WARN whose end is only DEBUG reads, to
    // anyone filtering at INFO, as a broker that never came back.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int failedAttempts = m_reconnectAttempts;
    m_logCollapse.flush(QStringLiteral("connecting"), nowMs);
    m_logCollapse.flush(QStringLiteral("retry"), nowMs);
    m_logCollapse.flush(QStringLiteral("mdns"), nowMs);
    m_logCollapse.flush(QStringLiteral("failed"), nowMs);

    if (failedAttempts > 0) {
        DIAG_INFO(NETWORK, "MqttClient").noquote()
            << QStringLiteral("Connected to broker after %1 failed attempt(s)").arg(failedAttempts);
    } else if (m_dropLogged) {
        DIAG_INFO(NETWORK, "MqttClient").noquote() << QStringLiteral("Reconnected to broker");
    } else {
        QString line = QStringLiteral("Connected to broker");
        if (m_useMqtt31) line += QStringLiteral(" (MQTT 3.1)");
        if (m_attemptTls) line += QStringLiteral(" (TLS)");
        DIAG_DEBUG(NETWORK, "MqttClient").noquote() << line;
    }

    m_sessionUp = true;
    m_dropLogged = false;
    m_reconnectTimer.stop();
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();

    // "Connected" waits for the subscription acknowledgements (updateVerifiedState()).
    setupSubscriptions();

    publishAvailability(true);

    if (m_settingsMqtt && m_settingsMqtt->mqttHomeAssistantDiscovery()) {
        publishHomeAssistantDiscovery();
    } else if (m_settingsMqtt && !m_settingsMqtt->mqttPublishedDiscoveryComponents().isEmpty()) {
        // Discovery was turned off while we were not connected.
        retractDiscovery();
    }

    const int interval = m_settingsMqtt ? m_settingsMqtt->mqttPublishInterval() : 1000;
    m_publishTimer.start(interval);

    republishAll();
}

void MqttClient::onSessionDown()
{
    endSession();

    // A disconnect the USER asked for is not a fault to recover from.
    if (m_userRequestedDisconnect) {
        m_userRequestedDisconnect = false;
        m_dropLogged = false;
        m_reconnectTimer.stop();
        m_status = "Disconnected";
        emit statusChanged();
        return;
    }

    // Qt MQTT reports a missed keepalive as ServerUnavailable (qmqttconnection.cpp:702-703);
    // anything else is the socket's own story.
    const QString reason = m_client->error() == QMqttClient::ServerUnavailable
        ? QStringLiteral("broker stopped answering")
        : failureReason(QMqttClient::NoError,
                        m_activeSocket ? m_activeSocket->error() : QAbstractSocket::UnknownSocketError, {});
    DIAG_INFO(NETWORK, "MqttClient").noquote() << "Disconnected from broker - " + reason;
    m_dropLogged = true;

    // Otherwise keep trying while enabled. A broker that accepts TCP and then drops the
    // session (stale ACL, duplicate client ID) only ever reaches here, so this path must
    // announce the fast->slow transition too — scheduleReconnect() does.
    if (m_settingsMqtt && m_settingsMqtt->mqttEnabled()) {
        scheduleReconnect(reason);
        return;
    }

    m_status = "Disconnected";
    emit statusChanged();
}

// The session's own state; status and any retry are the caller's.
void MqttClient::endSession()
{
    m_sessionUp = false;
    const bool wasConnected = m_connected;
    m_connected = false;
    m_requiredSubscriptions.clear();
    m_refusedSubscription.clear();
    m_publishTimer.stop();
    if (wasConnected)
        emit connectedChanged();
}

void MqttClient::onConnectionFailed(const QString& reason)
{
    // A failure whose reason has not changed adds nothing; a DIFFERENT one still warns
    // at once, and scheduleReconnect()'s one-shot "backing off" marks the giving-up moment.
    {
        LogCollapse::Collapsed collapsed;
        const QString text = QStringLiteral("Connection failed - ") + reason;
        if (m_logCollapse.shouldLog(QStringLiteral("failed"), text,
                                    QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
            DIAG_WARN(NETWORK, "MqttClient").noquote() << text + m_logCollapse.suffix(collapsed);
        }
    }

    endSession();
    m_status = "Error: " + reason;
    emit statusChanged();

    scheduleReconnect(reason);
}

void MqttClient::disconnectFromBroker()
{
    ++m_attemptGeneration;   // an mDNS answer still on its way must not reconnect
    m_reconnectTimer.stop();
    m_publishTimer.stop();
    m_attemptDeadline.stop();
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();

    if (m_pendingSocket)
        abandonSocket(m_pendingSocket);   // never reached Qt MQTT: no callback follows

    if (m_client->state() != QMqttClient::Disconnected) {
        // Armed only where a state change is expected to consume it; connectToBroker()
        // clears it regardless (tst_mqttclient covers the latch this prevents).
        m_userRequestedDisconnect = true;
        if (m_sessionUp && m_activeSocket && m_activeSocket->bytesToWrite() == 0) {
            publishAvailability(false);
            m_client->disconnectFromHost();
        } else if (m_activeSocket) {
            // Mid-handshake, or a stalled link where DISCONNECT would block for up to
            // 30 s (qmqttconnection.cpp:736): just close.
            m_activeSocket->abort();
        }
        return;
    }

    endSession();
    m_status = "Disconnected";
    emit statusChanged();
}

// Single arming point for every failure that should be RETRIED. connectToBroker()'s two
// configuration guards (null settings, empty host) deliberately do not come here: both set
// an actionable status and are recovered by onSettingsChanged() when the user fixes them.
void MqttClient::scheduleReconnect(const QString& reason)
{
    if (!m_settingsMqtt || !m_settingsMqtt->mqttEnabled()) {
        // Do NOT retry — but do not swallow the failure either: the Connect button and
        // the web/MCP connect paths can start an attempt while MQTT is disabled.
        DIAG_WARN(NETWORK, "MqttClient") << "connect attempt failed while MQTT is disabled -"
                   << "not retrying. Reason:" << reason;
        m_status = "Error: " + reason;
        emit statusChanged();
        return;
    }

    const int delay = reconnectDelayMs();
    if (m_reconnectAttempts >= MAX_FAST_RECONNECT_ATTEMPTS && !m_slowRetryAnnounced) {
        // Once: past here the fast budget is spent against a network that is up.
        m_slowRetryAnnounced = true;
        DIAG_WARN(NETWORK, "MqttClient") << "broker unreachable after" << m_reconnectAttempts
                   << "attempts - backing off to one retry every" << delay / 60000
                   << "min. Reason:" << reason;
    }
    {
        LogCollapse::Collapsed collapsed;
        const QString text = QStringLiteral("Retrying in %1 seconds - %2")
                                 .arg(delay / 1000).arg(reason);
        if (m_logCollapse.shouldLog(QStringLiteral("retry"), text,
                                    QDateTime::currentMSecsSinceEpoch(), &collapsed)) {
            DIAG_DEBUG(NETWORK, "MqttClient").noquote() << text + m_logCollapse.suffix(collapsed);
        }
    }
    m_reconnectTimer.start(delay);

    // Keep the broker's own words next to the retry schedule: both land in one event-loop
    // turn, so a bare reconnectStatusText() would erase "bad username or password".
    m_status = reconnectStatusText() + " (" + reason + ")";
    emit statusChanged();
}

QString MqttClient::reconnectStatusText() const
{
    if (m_reconnectAttempts >= MAX_FAST_RECONNECT_ATTEMPTS) {
        return QString("Disconnected - retrying every %1 min")
            .arg(IDLE_RECONNECT_DELAY_MS / 60000);
    }
    return QString("Disconnected - reconnecting (%1/%2)...")
        .arg(m_reconnectAttempts + 1)
        .arg(MAX_FAST_RECONNECT_ATTEMPTS);
}

void MqttClient::setupSubscriptions()
{
    // Qt MQTT never deletes a subscription; closing a connection only forgets it
    // (qmqttconnection.cpp:784, 868). Here, before the new ones exist, none of ours is in
    // its active map, so a destructor's unsubscribe() finds nothing to send.
    for (const auto& previous : std::as_const(m_sessionSubscriptions))
        delete previous.data();
    m_sessionSubscriptions.clear();
    m_requiredSubscriptions.clear();
    m_refusedSubscription.clear();

    // Required: a broker account that may not read these looks connected while every
    // Home Assistant command silently goes nowhere.
    for (const QString& sub : {QStringLiteral("command"), QStringLiteral("profile/set"),
                               QStringLiteral("profile/select"), QStringLiteral("recipe/select")}) {
        QMqttSubscription* subscription = m_client->subscribe(QMqttTopicFilter(topicPath(sub)), 1);
        if (!subscription) {
            DIAG_WARN(NETWORK, "MqttClient") << "Failed to subscribe to" << topicPath(sub);
            continue;
        }
        m_requiredSubscriptions.append(subscription);
        m_sessionSubscriptions.append(subscription);
        connect(subscription, &QMqttSubscription::stateChanged, this,
                [this, subscription]() { onSubscriptionState(subscription); });
    }

    // Not required: only drives the Home Assistant restart recovery.
    if (QMqttSubscription* haStatus = m_client->subscribe(QMqttTopicFilter(QString::fromLatin1(kHomeAssistantStatusTopic)), 1)) {
        m_sessionSubscriptions.append(haStatus);
        connect(haStatus, &QMqttSubscription::stateChanged, this, [haStatus]() {
            if (haStatus->state() == QMqttSubscription::Error)
                DIAG_INFO(NETWORK, "MqttClient") << "broker refused" << kHomeAssistantStatusTopic
                          << "- Home Assistant restarts will not trigger a re-announce";
        });
    }
    updateVerifiedState();
}

void MqttClient::onSubscriptionState(QMqttSubscription* subscription)
{
    if (subscription->state() == QMqttSubscription::Error) {
        m_refusedSubscription = subscription->topic().filter();
        DIAG_WARN(NETWORK, "MqttClient") << "broker refused subscription to" << m_refusedSubscription;
    }
    updateVerifiedState();
}

void MqttClient::updateVerifiedState()
{
    if (!m_sessionUp)
        return;

    bool allSubscribed = !m_requiredSubscriptions.isEmpty();
    for (const auto& sub : m_requiredSubscriptions) {
        if (!sub || sub->state() != QMqttSubscription::Subscribed)
            allSubscribed = false;
    }

    QString status;
    bool verified = false;
    if (!m_refusedSubscription.isEmpty()) {
        status = QStringLiteral("Error: broker refused the subscription to %1 - check this account's permissions")
                     .arg(m_refusedSubscription);
    } else if (allSubscribed) {
        status = QStringLiteral("Connected");
        verified = true;
    } else {
        status = QString::fromLatin1(kConnecting);
    }

    // Both fields before either signal: ShotServer's connect endpoint reads isConnected()
    // inside statusChanged, and saw "Connected" with false.
    const bool statusChangedNow = status != m_status;
    const bool connectedChangedNow = verified != m_connected;
    m_status = status;
    m_connected = verified;
    if (connectedChangedNow && verified)
        DIAG_DEBUG(NETWORK, "MqttClient") << "Subscriptions acknowledged - connection verified";
    if (statusChangedNow)
        emit statusChanged();
    if (connectedChangedNow)
        emit connectedChanged();
}

void MqttClient::onMessageReceived(const QByteArray& message, const QString& topic)
{
    const QString payload = QString::fromUtf8(message);
    DIAG_DEBUG(NETWORK, "MqttClient") << "Received message on" << topic << ":" << payload;

    if (topic == QLatin1String(kHomeAssistantStatusTopic)) {
        if (payload.trimmed() == QLatin1String("online") && m_settingsMqtt
            && m_settingsMqtt->mqttHomeAssistantDiscovery()) {
            DIAG_INFO(NETWORK, "MqttClient") << "Home Assistant came online - re-announcing discovery and state";
            publishHomeAssistantDiscovery();
            republishAll();
        }
    } else if (topic == topicPath("command")) {
        handleCommand(payload.trimmed().toLower());
    } else if (topic == topicPath("profile/set")) {
        // Profile filename (without .json)
        const QString profileName = payload.trimmed();
        if (!profileName.isEmpty()) {
            DIAG_DEBUG(NETWORK, "MqttClient") << "Profile selection requested:" << profileName;
            emit profileSelectRequested(profileName);
        }
    } else if (topic == topicPath("recipe/select")) {
        const QString name = payload.trimmed();
        if (!name.isEmpty()) {
            DIAG_DEBUG(NETWORK, "MqttClient") << "Recipe selection requested:" << name;
            emit recipeTitleSelectRequested(name);
        }
    } else if (topic == topicPath("profile/select")) {
        // Profile title, from the Home Assistant select entity
        const QString title = payload.trimmed();
        if (!title.isEmpty()) {
            DIAG_DEBUG(NETWORK, "MqttClient") << "Profile selection requested by title:" << title;
            emit profileTitleSelectRequested(title);
        }
    }
}

bool MqttClient::stopAllowedInCurrentPhase() const
{
    if (!m_device || !m_device->isConnected() || !m_machineState)
        return false;
    // Beverage and rinse operations only. Descale and clean are unattended maintenance
    // cycles a dashboard tap must not interrupt (narrower than MCP machine_stop).
    using Phase = MachineState::Phase;
    switch (m_machineState->phase()) {
    case Phase::EspressoPreheating:
    case Phase::Preinfusion:
    case Phase::Pouring:
    case Phase::Ending:
    case Phase::Steaming:
    case Phase::HotWater:
    case Phase::Flushing:
        return true;
    default:
        return false;
    }
}

void MqttClient::handleCommand(const QString& command)
{
    if (command == "wake") {
        if (m_device) {
            m_device->wakeUp();
            DIAG_DEBUG(NETWORK, "MqttClient") << "Wake command executed";
        }
        emit commandReceived("wake");
    } else if (command == "sleep") {
        if (m_device) {
            m_device->goToSleep();
            DIAG_DEBUG(NETWORK, "MqttClient") << "Sleep command executed";
        }
        emit commandReceived("sleep");
    } else if (command == "steam_on") {
        emit steamOnRequested();
        emit commandReceived("steam_on");
        DIAG_DEBUG(NETWORK, "MqttClient") << "Steam on command executed";
    } else if (command == "steam_off") {
        emit steamOffRequested();
        emit commandReceived("steam_off");
        DIAG_DEBUG(NETWORK, "MqttClient") << "Steam off command executed";
    } else if (command == "stop") {
        if (!stopAllowedInCurrentPhase()) {
            if (!m_device || !m_device->isConnected() || !m_machineState) {
                DIAG_INFO(NETWORK, "MqttClient") << "Stop command ignored - DE1 not connected";
            } else {
                DIAG_INFO(NETWORK, "MqttClient").noquote()
                    << "Stop command ignored - no espresso, steam, hot water or flush running (phase: "
                       + m_machineState->phaseString() + ")";
            }
            return;
        }
        DIAG_INFO(NETWORK, "MqttClient") << "Stop command accepted in phase" << m_machineState->phaseString();
        emit stopRequested();
        emit commandReceived("stop");
    } else {
        DIAG_WARN(NETWORK, "MqttClient") << "Unknown command:" << command;
    }
}

void MqttClient::setCurrentProfile(const QString& profile)
{
    if (m_currentProfile != profile) {
        m_currentProfile = profile;
        emit currentProfileChanged();

        if (m_sessionUp && profile != m_lastPublishedProfile) {
            publish(topicPath("profile"), profile, true);
            m_lastPublishedProfile = profile;
            DIAG_DEBUG(NETWORK, "MqttClient") << "Published profile change:" << profile;
        }
    }
}

void MqttClient::setCurrentProfileFilename(const QString& filename)
{
    if (m_currentProfileFilename != filename) {
        m_currentProfileFilename = filename;

        if (m_sessionUp && !filename.isEmpty()) {
            publish(topicPath("profile_filename"), filename, true);
            DIAG_DEBUG(NETWORK, "MqttClient") << "Published profile filename change:" << filename;
        }
    }
}

void MqttClient::setMainController(MainController* controller)
{
    m_mainController = controller;
}

void MqttClient::setRecipeTitles(const QStringList& titles)
{
    if (titles == m_recipeTitles)
        return;
    m_recipeTitles = titles;
    if (m_sessionUp && m_settingsMqtt && m_settingsMqtt->mqttHomeAssistantDiscovery())
        publishHomeAssistantDiscovery();
}

void MqttClient::setActiveRecipe(const QString& name)
{
    if (name == m_activeRecipe)
        return;
    m_activeRecipe = name;
    publishActiveRecipe();
}

void MqttClient::publishActiveRecipe()
{
    // "None" is how an MQTT select is told it has no current option; an empty or
    // unknown value would be logged by Home Assistant as an invalid option.
    publish(topicPath("recipe"), m_activeRecipe.isEmpty() ? QStringLiteral("None") : m_activeRecipe, true);
}

void MqttClient::setProfileTitles(const QStringList& titles)
{
    if (titles == m_profileTitles)
        return;
    m_profileTitles = titles;
    if (!m_sessionUp || !m_settingsMqtt || !m_settingsMqtt->mqttHomeAssistantDiscovery())
        return;
    // The select's options are the profile list. Re-announcing the whole set also
    // retracts the select if the list just became empty.
    publishHomeAssistantDiscovery();
}

void MqttClient::onReconnectTimerTick()
{
    if (!m_settingsMqtt || !m_settingsMqtt->mqttEnabled()) {
        return;
    }

    // No network: the attempt would fail on the interface, not at the broker, so don't
    // charge it to the budget. Deliberately does NOT re-arm: onNetworkReachabilityChanged()
    // resumes on the event.
    if (m_networkDown) {
        DIAG_DEBUG(NETWORK, "MqttClient") << "reconnect deferred - no network (attempt"
                 << (m_reconnectAttempts + 1) << "not spent)";
        m_status = kWaitingForNetwork;
        emit statusChanged();
        return;
    }

    m_reconnectAttempts++;
    emit reconnectAttemptsChanged();

    if (m_reconnectAttempts > MAX_FAST_RECONNECT_ATTEMPTS) {
        DIAG_DEBUG(NETWORK, "MqttClient") << "Reconnection attempt" << m_reconnectAttempts << "(slow retry)";
    } else {
        DIAG_DEBUG(NETWORK, "MqttClient") << "Reconnection attempt" << m_reconnectAttempts
                 << "of" << MAX_FAST_RECONNECT_ATTEMPTS << "before backing off";
    }

    // Preserved until connectWithHost() reads it (may be async on Android)
    m_isReconnecting = true;
    connectToBroker();
}

void MqttClient::onNetworkReachabilityChanged(bool reachable)
{
    const bool nowDown = !reachable;
    if (m_networkDown == nowDown)
        return;
    m_networkDown = nowDown;

    if (m_networkDown) {
        DIAG_DEBUG(NETWORK, "MqttClient") << "network down - reconnect attempts suspended";
        m_reconnectTimer.stop();
        // Say so even while the session is nominally up: the keepalive takes two to three
        // intervals (~2-3 min) to notice (qmqttconnection.cpp:702-703), and publishes
        // meanwhile go nowhere behind a "Connected" dot.
        m_status = m_sessionUp ? kConnectedNetworkUnreachable : kWaitingForNetwork;
        emit statusChanged();
        return;
    }

    DIAG_DEBUG(NETWORK, "MqttClient") << "network back - resuming reconnect";

    // Clear BOTH statuses the down-edge can write, before any early return: a blip the TCP
    // session survives would otherwise leave "Connected - network unreachable" up forever.
    if (m_status == QLatin1String(kWaitingForNetwork)) {
        m_status = "Disconnected";
        emit statusChanged();
    } else if (m_status == QLatin1String(kConnectedNetworkUnreachable)) {
        if (m_sessionUp) {
            updateVerifiedState();   // also "awaiting acknowledgement" or a refused topic
        } else {
            m_status = "Disconnected";
            emit statusChanged();
        }
    }

    if (!m_settingsMqtt || !m_settingsMqtt->mqttEnabled() || m_sessionUp)
        return;

    // A regained network gets a full FAST budget and an immediate attempt.
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();
    m_reconnectTimer.stop();
    m_isReconnecting = false;
    connectToBroker();
}

void MqttClient::onSettingsChanged()
{
    m_useMqtt31 = false;   // a different broker (or TLS endpoint) gets 3.1.1 first again

    if (m_client->state() != QMqttClient::Disconnected || m_pendingSocket) {
        disconnectFromBroker();
    }

    if (m_settingsMqtt && m_settingsMqtt->mqttEnabled()) {
        connectToBroker();
        return;
    }

    m_reconnectTimer.stop();
    m_reconnectAttempts = 0;
    m_slowRetryAnnounced = false;
    emit reconnectAttemptsChanged();
    m_status = "Disabled";
    emit statusChanged();
}

void MqttClient::onDiscoverySettingChanged()
{
    if (!m_sessionUp || !m_settingsMqtt)
        return;   // reconciled on the next connect (onSessionUp)
    if (m_settingsMqtt->mqttHomeAssistantDiscovery()) {
        publishHomeAssistantDiscovery();
        republishAll();
    } else {
        retractDiscovery();
    }
}

QString MqttClient::topicPath(const QString& subtopic) const
{
    QString baseTopic = m_settingsMqtt ? m_settingsMqtt->mqttBaseTopic() : "decenza";
    return baseTopic + "/" + subtopic;
}

void MqttClient::publish(const QString& topic, const QString& payload, bool retain)
{
    publishRaw(topic, payload, retain && m_settingsMqtt && m_settingsMqtt->mqttRetainMessages(), 0);
}

void MqttClient::publishRaw(const QString& topic, const QString& payload, bool retain, quint8 qos)
{
#ifdef DECENZA_TESTING
    if (m_publishRecorder) {
        m_publishRecorder->append({topic, payload, retain, qos});
        return;
    }
#endif
    if (!m_sessionUp)
        return;
    if (m_client->publish(QMqttTopicName(topic), payload.toUtf8(), qos, retain) < 0) {
        DIAG_WARN(NETWORK, "MqttClient") << "Failed to publish to" << topic;
    }
}

// What Home Assistant's availability reads: always retained, like the will and the exit
// "offline". With the user's retain setting off, a non-retained "online" left the retained
// "offline" on the broker, and every entity Home Assistant (re)subscribed read unavailable.
void MqttClient::publishAvailabilityInput(const QString& subtopic, const QString& payload)
{
    publishRaw(topicPath(subtopic), payload, true, 1);
}

void MqttClient::publishAvailability(bool online)
{
    publishAvailabilityInput(QStringLiteral("availability"), online ? "online" : "offline");
}

void MqttClient::republishAll()
{
    // Forget what was sent so the full state goes out again (the broker or Home
    // Assistant may have lost it).
    m_lastPublishedState.clear();
    m_lastPublishedPhase.clear();
    m_lastPublishedSubstate.clear();
    m_lastPublishedProfile.clear();
    m_lastPublishedSteamMode.clear();
    m_lastPublishedEspressoCount = -1;

    publishAvailability(true);
    // Machine and scale entities are available only while these read "true".
    publishAvailabilityInput(QStringLiteral("connected"), (m_device && m_device->isConnected()) ? "true" : "false");
    publishAvailabilityInput(QStringLiteral("scale_connected"), m_scaleConnected ? "true" : "false");
    m_lastPublishedScaleConnected = m_scaleConnected;
    publishState();
    publishActiveRecipe();
    onPublishTimerTick();
    onWaterLevelChanged();

    if (m_haveLastShot)
        publishLastShot();
    else
        emit lastShotRequested();
}

void MqttClient::onPhaseChanged()
{
    publishState();
}

void MqttClient::onDE1StateChanged()
{
    publishState();
}

void MqttClient::onDE1ConnectedChanged()
{
    if (!m_sessionUp) return;

    bool connected = m_device && m_device->isConnected();
    publishAvailabilityInput(QStringLiteral("connected"), connected ? "true" : "false");
}

void MqttClient::onWaterLevelChanged()
{
    if (!m_sessionUp || !m_device) return;

    publish(topicPath("water_level"), QString::number(static_cast<int>(m_device->waterLevel())), true);
    publish(topicPath("water_level_ml"), QString::number(m_device->waterLevelMl()), true);
}

void MqttClient::onScaleConnectedChanged(bool connected)
{
    m_scaleConnected = connected;
    if (!m_sessionUp) return;

    if (connected != m_lastPublishedScaleConnected) {
        publishAvailabilityInput(QStringLiteral("scale_connected"), connected ? "true" : "false");
        m_lastPublishedScaleConnected = connected;
        DIAG_DEBUG(NETWORK, "MqttClient") << "Published scale connected:" << connected;
    }
}

void MqttClient::onSteamSettingsChanged()
{
    publishState();
}

void MqttClient::publishState()
{
    if (!m_sessionUp || !m_device) return;

    QString state = m_device->stateString();
    QString substate = m_device->subStateString();
    QString phase = m_machineState ? m_machineState->phaseString() : "Unknown";

    if (state != m_lastPublishedState) {
        publish(topicPath("state"), state, true);
        m_lastPublishedState = state;
    }

    if (phase != m_lastPublishedPhase) {
        publish(topicPath("phase"), phase, true);
        m_lastPublishedPhase = phase;
    }

    if (!m_currentProfile.isEmpty() && m_currentProfile != m_lastPublishedProfile) {
        publish(topicPath("profile"), m_currentProfile, true);
        m_lastPublishedProfile = m_currentProfile;
    }

    if (!m_currentProfileFilename.isEmpty()) {
        publish(topicPath("profile_filename"), m_currentProfileFilename, true);
    }

    // Steam mode reports what the MACHINE is doing: Ready/Steaming are On whatever we
    // last commanded. Below that it asks SteamHeaterPolicy, the only home of the rule.
    QString steamMode;
    auto* policy = m_mainController ? m_mainController->steamHeaterPolicy() : nullptr;
    if (!m_device || !m_settings || m_device->stateString() == "Sleep") {
        steamMode = "Off";
    } else if (phase == "Ready" || phase == "Steaming") {
        steamMode = "On";
    } else if (!policy) {
        steamMode = "Off";
    } else {
        steamMode = policy->resolve().on ? "On" : "Off";
    }
    if (steamMode != m_lastPublishedSteamMode) {
        publish(topicPath("steam_mode"), steamMode, true);
        publish(topicPath("steam_state"), steamMode != "Off" ? "true" : "false", true);
        m_lastPublishedSteamMode = steamMode;
    }

    if (substate != m_lastPublishedSubstate) {
        publish(topicPath("substate"), substate, true);
        m_lastPublishedSubstate = substate;
    }
}

void MqttClient::onPublishTimerTick()
{
    if (!m_sessionUp) return;

    if (m_device) {
        publish(topicPath("temperature/head"), QString::number(m_device->temperature(), 'f', 1), true);
        publish(topicPath("temperature/mix"), QString::number(m_device->mixTemperature(), 'f', 1), true);
        publish(topicPath("temperature/steam"), QString::number(m_device->steamTemperature(), 'f', 1), true);
        publish(topicPath("pressure"), QString::number(m_device->pressure(), 'f', 2), true);
        publish(topicPath("flow"), QString::number(m_device->flow(), 'f', 2), true);
    }

    if (m_machineState) {
        publish(topicPath("weight"), QString::number(m_machineState->scaleWeight(), 'f', 1), true);
        publish(topicPath("shot_time"), QString::number(m_machineState->shotTime(), 'f', 1), true);
        publish(topicPath("target_weight"), QString::number(m_machineState->targetWeight(), 'f', 1), true);
    }

    if (m_mainController && m_mainController->shotHistory()) {
        int count = m_mainController->shotHistory()->totalShots();
        if (count != m_lastPublishedEspressoCount) {
            publish(topicPath("espresso_count"), QString::number(count), true);
            m_lastPublishedEspressoCount = count;
        }
    }
}

// ===== Shot lifecycle =====

void MqttClient::publishShotEvent(const QString& eventType, const QJsonObject& detail)
{
    QJsonObject event = detail;
    event["event_type"] = eventType;
    // Events are moments, not state: never retained, whatever the retain setting.
    publishRaw(topicPath("event/shot"),
               QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)), false, 0);
}

void MqttClient::onEspressoCycleStarted(bool maintenance)
{
    // History never saves maintenance runs, so neither the start nor any outcome is
    // reported for them.
    m_shotCycleOpen = !maintenance;
    if (m_shotCycleOpen)
        publishShotEvent(QStringLiteral("started"), {{"profile", m_currentProfile}});
}

void MqttClient::onShotPersisted(double durationSec, double yieldG)
{
    if (!m_shotCycleOpen)
        return;
    m_shotCycleOpen = false;
    QJsonObject detail{{"profile", m_currentProfile},
                       {"duration_s", qRound(durationSec * 10) / 10.0}};
    if (yieldG > 0)
        detail["yield_g"] = qRound(yieldG * 10) / 10.0;
    publishShotEvent(QStringLiteral("finished"), detail);
}

void MqttClient::onShotNotSaved()
{
    if (!m_shotCycleOpen)
        return;
    m_shotCycleOpen = false;
    publishShotEvent(QStringLiteral("aborted"), {{"profile", m_currentProfile}});
}

QJsonObject MqttClient::lastShotSummary(const LastShot& shot)
{
    // Omitted rather than zero: a 0 g yield reads as a real measurement in a dashboard.
    QJsonObject summary;
    if (shot.startEpochSec > 0) {
        const QDateTime finished = QDateTime::fromSecsSinceEpoch(shot.startEpochSec)
                                       .addMSecs(qRound64(shot.durationSec * 1000));
        summary["finished_at"] = finished.toOffsetFromUtc(finished.offsetFromUtc()).toString(Qt::ISODate);
    }
    if (shot.durationSec > 0)
        summary["duration_s"] = qRound(shot.durationSec * 10) / 10.0;
    if (shot.doseG > 0)
        summary["dose_g"] = qRound(shot.doseG * 10) / 10.0;
    if (shot.yieldG > 0)
        summary["yield_g"] = qRound(shot.yieldG * 10) / 10.0;
    if (shot.doseG > 0 && shot.yieldG > 0)
        summary["ratio"] = qRound(shot.yieldG / shot.doseG * 100) / 100.0;
    if (!shot.profile.isEmpty())
        summary["profile"] = shot.profile;
    return summary;
}

void MqttClient::setLastShot(const LastShot& shot)
{
    m_lastShot = shot;
    m_haveLastShot = true;
    publishLastShot();
}

void MqttClient::publishLastShot()
{
    // Retained regardless of the retain setting: it is the one value that cannot be
    // re-derived from live telemetry after a broker or Home Assistant restart.
    publishRaw(topicPath("last_shot"),
               QString::fromUtf8(QJsonDocument(lastShotSummary(m_lastShot)).toJson(QJsonDocument::Compact)),
               true, 0);
}

// ===== Home Assistant discovery =====
//
// One device-based discovery message (Home Assistant 2024.11+) at
// homeassistant/device/<device ID>/config. Builds before it published one message per
// entity at homeassistant/<component>/de1_<object>/config; those move over once
// (publishHomeAssistantDiscovery()) by Home Assistant's documented migrate_discovery steps.

void MqttClient::publishDiscovery()
{
    if (m_sessionUp) {
        publishHomeAssistantDiscovery();
    }
}

QJsonObject MqttClient::buildDeviceInfo() const
{
    QJsonObject device;
    device["identifiers"] = QJsonArray{QString("decenza_de1_%1").arg(deviceId())};
    device["name"] = "DE1 Espresso Machine";
    device["manufacturer"] = "Decent Espresso";
    device["model"] = "DE1";
    device["sw_version"] = VERSION_STRING;
    return device;
}

QString MqttClient::legacyDiscoveryTopic(const QString& component, const QString& objectId)
{
    return QString("homeassistant/%1/de1_%2/config").arg(component, objectId);
}

QStringList MqttClient::legacyDiscoveryTopics()
{
    // Exactly what builds before device discovery published; frozen, not derived from
    // discoveryEntries(), which grows.
    static const QList<std::pair<const char*, const char*>> shipped = {
        {"sensor", "state"}, {"sensor", "phase"}, {"sensor", "substate"},
        {"sensor", "temperature_head"}, {"sensor", "temperature_mix"}, {"sensor", "temperature_steam"},
        {"sensor", "pressure"}, {"sensor", "flow"}, {"sensor", "weight"}, {"sensor", "target_weight"},
        {"sensor", "water_level"}, {"sensor", "water_level_ml"}, {"sensor", "shot_time"},
        {"text", "profile"}, {"switch", "power"}, {"binary_sensor", "connected"},
        {"binary_sensor", "scale_connected"}, {"sensor", "steam_mode"}, {"switch", "steam"},
        {"sensor", "espresso_count"}, {"sensor", "profile_filename"},
    };
    QStringList topics;
    for (const auto& [component, objectId] : shipped)
        topics << legacyDiscoveryTopic(QString::fromLatin1(component), QString::fromLatin1(objectId));
    return topics;
}

QString MqttClient::deviceDiscoveryTopic() const
{
    // Home Assistant allows [a-zA-Z0-9_-] in a topic's object ID; a device ID seeded from a
    // hand-typed client ID may hold anything else.
    QString objectId = deviceId();
    for (QChar& c : objectId) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != '_' && c != '-')
            c = '_';
    }
    return QStringLiteral("homeassistant/device/%1/config").arg(objectId);
}

QList<MqttClient::DiscoveryEntry> MqttClient::discoveryEntries() const
{
    const QString base = m_settingsMqtt ? m_settingsMqtt->mqttBaseTopic() : QStringLiteral("decenza");
    QList<DiscoveryEntry> e;

    // objectId is the component key in the device message; uniqueSuffix is the unique_id
    // tail. Both are frozen as shipped: unique_id is how Home Assistant matches an
    // existing entity.
    auto add = [&e](const QString& component, const QString& objectId, const QString& uniqueSuffix,
                    Source source, QJsonObject config) {
        config["unique_id"] = uniqueSuffix;   // completed with the device ID in componentConfig()
        e.append({component, objectId, config, source});
    };
    auto sensor = [&](const QString& objectId, const QString& uniqueSuffix, Source source,
                      const QString& name, const QString& topic, QJsonObject extra = {}) {
        extra["name"] = name;
        extra["state_topic"] = base + "/" + topic;
        add("sensor", objectId, uniqueSuffix, source, extra);
    };
    const QString degC = QStringLiteral("°C");

    sensor("state", "state", Source::Machine, "DE1 State", "state", {{"icon", "mdi:coffee-maker"}});
    sensor("phase", "phase", Source::Machine, "DE1 Phase", "phase", {{"icon", "mdi:coffee-maker-outline"}});
    sensor("substate", "substate", Source::Machine, "DE1 Substate", "substate", {{"icon", "mdi:information-outline"}});
    sensor("temperature_head", "temp_head", Source::Machine, "DE1 Head Temperature", "temperature/head",
           {{"device_class", "temperature"}, {"unit_of_measurement", degC}});
    sensor("temperature_mix", "temp_mix", Source::Machine, "DE1 Mix Temperature", "temperature/mix",
           {{"device_class", "temperature"}, {"unit_of_measurement", degC}});
    sensor("temperature_steam", "temp_steam", Source::Machine, "DE1 Steam Temperature", "temperature/steam",
           {{"device_class", "temperature"}, {"unit_of_measurement", degC}, {"icon", "mdi:water-boiler"}});
    sensor("pressure", "pressure", Source::Machine, "DE1 Pressure", "pressure",
           {{"device_class", "pressure"}, {"unit_of_measurement", "bar"}});
    sensor("flow", "flow", Source::Machine, "DE1 Flow", "flow",
           {{"unit_of_measurement", "ml/s"}, {"icon", "mdi:water-flow"}});
    sensor("weight", "weight", Source::Scale, "DE1 Weight", "weight",
           {{"device_class", "weight"}, {"unit_of_measurement", "g"}});
    sensor("target_weight", "target_weight", Source::App, "DE1 Target Weight", "target_weight",
           {{"device_class", "weight"}, {"unit_of_measurement", "g"}, {"icon", "mdi:scale-balance"}});
    sensor("water_level", "water_level", Source::Machine, "DE1 Water Level", "water_level",
           {{"unit_of_measurement", "%"}, {"icon", "mdi:water"}});
    sensor("water_level_ml", "water_level_ml", Source::Machine, "DE1 Water Level (ml)", "water_level_ml",
           {{"unit_of_measurement", "ml"}, {"icon", "mdi:water"}});
    sensor("shot_time", "shot_time", Source::Machine, "DE1 Shot Time", "shot_time",
           {{"unit_of_measurement", "s"}, {"icon", "mdi:timer"}});

    // Superseded by the profile select below (it shows the title but takes a filename).
    // Home Assistant applies enabled_by_default only when it first creates the entity, so
    // existing users keep theirs; new devices show only the select.
    add("text", "profile", "profile", Source::App,
        {{"name", "DE1 Profile"}, {"state_topic", base + "/profile"},
         {"command_topic", base + "/profile/set"}, {"icon", "mdi:coffee"},
         {"enabled_by_default", false}});

    add("switch", "power", "power", Source::Machine,
        {{"name", "DE1 Power"}, {"command_topic", base + "/command"}, {"state_topic", base + "/state"},
         {"payload_on", "wake"}, {"payload_off", "sleep"}, {"state_on", "ON"}, {"state_off", "OFF"},
         {"value_template", "{{ 'OFF' if value in ['Sleep', 'GoingToSleep'] else 'ON' }}"},
         {"icon", "mdi:power"}});

    add("binary_sensor", "connected", "connected", Source::App,
        {{"name", "DE1 Connected"}, {"state_topic", base + "/connected"},
         {"payload_on", "true"}, {"payload_off", "false"}, {"device_class", "connectivity"},
         {"icon", "mdi:bluetooth-connect"}});
    add("binary_sensor", "scale_connected", "scale_connected", Source::App,
        {{"name", "DE1 Scale Connected"}, {"state_topic", base + "/scale_connected"},
         {"payload_on", "true"}, {"payload_off", "false"}, {"device_class", "connectivity"},
         {"icon", "mdi:scale"}});

    sensor("steam_mode", "steam_mode", Source::Machine, "DE1 Steam Mode", "steam_mode", {{"icon", "mdi:water-boiler"}});
    add("switch", "steam", "steam", Source::Machine,
        {{"name", "DE1 Steam"}, {"command_topic", base + "/command"}, {"state_topic", base + "/steam_state"},
         {"payload_on", "steam_on"}, {"payload_off", "steam_off"}, {"state_on", "true"}, {"state_off", "false"},
         {"icon", "mdi:water-boiler"}});

    // total_increasing: HA treats a decrease (history cleared) as a meter reset.
    sensor("espresso_count", "espresso_count", Source::App, "DE1 Espresso Count", "espresso_count",
           {{"icon", "mdi:counter"}, {"state_class", "total_increasing"}});
    sensor("profile_filename", "profile_filename", Source::App, "DE1 Profile Filename", "profile_filename",
           {{"icon", "mdi:file-document"}});

    QJsonArray options;
    for (const QString& title : m_profileTitles)
        options.append(title);
    if (!options.isEmpty()) {   // HA rejects a select with no options
        add("select", "profile_select", "profile_select", Source::App,
            {{"name", "DE1 Profile Select"}, {"state_topic", base + "/profile"},
             {"command_topic", base + "/profile/select"}, {"options", options},
             {"icon", "mdi:coffee"}});
    }

    QJsonArray recipes;
    for (const QString& name : m_recipeTitles)
        recipes.append(name);
    if (!recipes.isEmpty()) {
        add("select", "recipe_select", "recipe_select", Source::App,
            {{"name", "DE1 Recipe"}, {"state_topic", base + "/recipe"},
             {"command_topic", base + "/recipe/select"}, {"options", recipes},
             {"icon", "mdi:clipboard-list-outline"}});
    }

    auto lastShot = [&](const QString& key, const QString& name, QJsonObject extra) {
        extra["name"] = name;
        extra["state_topic"] = base + "/last_shot";
        extra["value_template"] = QStringLiteral("{{ value_json.%1 | default(None) }}").arg(key);
        add("sensor", "last_shot_" + key, "last_shot_" + key, Source::App, extra);
    };
    lastShot("finished_at", "DE1 Last Shot Time", {{"device_class", "timestamp"}, {"icon", "mdi:clock-check"}});
    lastShot("duration_s", "DE1 Last Shot Duration",
             {{"device_class", "duration"}, {"unit_of_measurement", "s"}, {"icon", "mdi:timer"}});
    lastShot("dose_g", "DE1 Last Shot Dose",
             {{"device_class", "weight"}, {"unit_of_measurement", "g"}, {"icon", "mdi:coffee-outline"}});
    lastShot("yield_g", "DE1 Last Shot Yield",
             {{"device_class", "weight"}, {"unit_of_measurement", "g"}, {"icon", "mdi:cup"}});
    lastShot("ratio", "DE1 Last Shot Ratio", {{"icon", "mdi:division"}});
    lastShot("profile", "DE1 Last Shot Profile", {{"icon", "mdi:coffee"}});

    add("event", "shot", "shot_event", Source::App,
        {{"name", "DE1 Shot"}, {"state_topic", base + "/event/shot"},
         {"event_types", QJsonArray{"started", "finished", "aborted"}}, {"icon", "mdi:coffee-to-go"}});

    add("button", "stop", "stop", Source::Machine,
        {{"name", "DE1 Stop"}, {"command_topic", base + "/command"}, {"payload_press", "stop"},
         {"icon", "mdi:stop-circle"}});

    return e;
}

QJsonObject MqttClient::componentConfig(const DiscoveryEntry& entry) const
{
    const QString base = m_settingsMqtt ? m_settingsMqtt->mqttBaseTopic() : QStringLiteral("decenza");
    QJsonObject config = entry.config;
    config["platform"] = entry.component;
    config["unique_id"] = QString("de1_%1_%2").arg(deviceId(), entry.config.value("unique_id").toString());

    // Per component, not shared at device level: Home Assistant rejects a config with both
    // availability_topic and an availability list.
    if (entry.source == Source::App) {
        config["availability_topic"] = base + "/availability";
    } else {
        // Available only while the app is online AND the device it reads is connected,
        // so a disconnected DE1 shows "unavailable" rather than its last value.
        const QString deviceTopic = base + (entry.source == Source::Machine ? "/connected" : "/scale_connected");
        config["availability"] = QJsonArray{
            QJsonObject{{"topic", base + "/availability"}},
            QJsonObject{{"topic", deviceTopic}, {"payload_available", "true"}, {"payload_not_available", "false"}},
        };
        config["availability_mode"] = "all";
    }
    return config;
}

QJsonObject MqttClient::deviceDiscoveryPayload(QStringList* publishedComponents) const
{
    QJsonObject components;
    QStringList current;
    for (const DiscoveryEntry& entry : discoveryEntries()) {
        components[entry.objectId] = componentConfig(entry);
        current << entry.objectId + "=" + entry.component;
    }
    // One that has left the set goes out once with only its platform; Home Assistant
    // needs that before the key may be omitted.
    if (m_settingsMqtt) {
        for (const QString& previous : m_settingsMqtt->mqttPublishedDiscoveryComponents()) {
            const QString key = previous.section('=', 0, 0);
            if (!components.contains(key))
                components[key] = QJsonObject{{"platform", previous.section('=', 1)}};
        }
    }
    if (publishedComponents)
        *publishedComponents = current;

    return QJsonObject{
        {"device", buildDeviceInfo()},
        {"origin", QJsonObject{{"name", "Decenza"}, {"sw_version", VERSION_STRING},
                               {"support_url", "https://github.com/Kulitorum/Decenza"}}},
        {"components", components},
    };
}

void MqttClient::publishHomeAssistantDiscovery()
{
    if (!m_settingsMqtt) return;

    // Once per identity: move the per-entity topics of earlier builds. Order is Home
    // Assistant's: migrate_discovery (not retained) on each old topic, then the device
    // message, then empty retained payloads on the old topics, sent in that order on one
    // connection.
    const bool migrating = !m_settingsMqtt->mqttDiscoveryMigrated();
    const QStringList legacy = migrating ? legacyDiscoveryTopics() : QStringList();
    if (migrating) {
        for (const QString& topic : legacy)
            publishRaw(topic, QStringLiteral("{\"migrate_discovery\":true}"), false, 1);
    }

    QStringList components;
    const QJsonObject payload = deviceDiscoveryPayload(&components);
    publishRaw(deviceDiscoveryTopic(), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)),
               m_settingsMqtt->mqttRetainMessages(), 1);
    m_settingsMqtt->setMqttPublishedDiscoveryComponents(components);
    m_discoveryEntityCount = static_cast<int>(components.size());

    if (migrating) {
        for (const QString& topic : legacy)
            publishRaw(topic, QString(), true, 1);
        m_settingsMqtt->setMqttDiscoveryMigrated(true);
        DIAG_INFO(NETWORK, "MqttClient") << "Moved Home Assistant discovery to one device message ("
                  << legacy.size() << "per-entity topics cleared)";
    }

    DIAG_DEBUG(NETWORK, "MqttClient") << "Home Assistant discovery published —"
             << m_discoveryEntityCount << "entities";
}

void MqttClient::retractDiscovery()
{
    if (!m_settingsMqtt) return;
    // This install's own device topic. An empty RETAINED payload is what removes the
    // device, whatever the retain setting was when it was published.
    publishRaw(deviceDiscoveryTopic(), QString(), true, 1);
    if (!m_settingsMqtt->mqttDiscoveryMigrated()) {
        // Never moved: the entities are still on the per-entity topics, which carry no
        // device ID — shared by any install on this broker that never moved either.
        for (const QString& topic : legacyDiscoveryTopics())
            publishRaw(topic, QString(), true, 1);
        m_settingsMqtt->setMqttDiscoveryMigrated(true);
    }
    m_settingsMqtt->setMqttPublishedDiscoveryComponents({});
    DIAG_INFO(NETWORK, "MqttClient") << "Removed this device from Home Assistant discovery";
}
