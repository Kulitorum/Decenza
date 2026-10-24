#include "mqttservicesource.h"

#include "mqttservicepolicy.h"
#include "core/settings.h"
#include "core/settings_mqtt.h"
#include "network/mqttclient.h"
#include "network/shotserver.h"

#include <QJsonObject>
#include <QMetaType>
#include <QMetaObject>
#include <QCryptographicHash>
#include <QStringList>

namespace {
QString requestStartReason(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("busy");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}

QVariantMap failure(const QString& reason)
{
    return {{QStringLiteral("reason"), reason}};
}

bool exactStringArgument(const QVariantMap& arguments, const QString& key, QString* out,
                         bool required = true)
{
    if (!arguments.contains(key)) {
        if (!required) { *out = {}; return true; }
        return false;
    }
    const QVariant value = arguments.value(key);
    if (value.metaType().id() != QMetaType::QString)
        return false;
    *out = value.toString();
    return true;
}

bool hasOnlyKeys(const QVariantMap& arguments, const QStringList& allowed)
{
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
        if (!allowed.contains(it.key())) return false;
    }
    return true;
}
}

NativeMqttServiceSource::NativeMqttServiceSource(Settings* settings, MqttClient* client,
                                                 ShotServer* shotServer, QObject* parent)
    : MqttStateSimpleSource(parent), m_settings(settings), m_client(client), m_shotServer(shotServer)
{
    if (m_settings)
        connect(m_settings, &QObject::destroyed, this, [this] {
            m_settings = nullptr;
            publish();
        });
    if (m_settings && m_settings->mqtt()) {
        auto* mqtt = m_settings->mqtt();
        const auto changed = [this] { publish(); };
        connect(mqtt, &SettingsMqtt::mqttEnabledChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttBrokerHostChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttBrokerPortChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttUsernameChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttPasswordChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttBaseTopicChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttPublishIntervalChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttRetainMessagesChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttHomeAssistantDiscoveryChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttClientIdChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttUseTlsChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttCaCertificateChanged, this, changed);
        connect(mqtt, &SettingsMqtt::mqttDeviceIdChanged, this, changed);
    }
    if (m_client) {
        const auto changed = [this] { publish(); };
        connect(m_client, &MqttClient::connectedChanged, this, changed);
        connect(m_client, &MqttClient::statusChanged, this, changed);
        connect(m_client, &QObject::destroyed, this, [this] { publish(); });
    }
    if (m_shotServer) {
        const auto changed = [this] { publish(); };
        connect(m_shotServer, &ShotServer::runningChanged, this, changed);
        connect(m_shotServer, &ShotServer::urlChanged, this, changed);
        connect(m_shotServer, &QObject::destroyed, this, [this] { publish(); });
    }
    publish();
}

void NativeMqttServiceSource::refresh()
{
    publish();
}

QVariantMap NativeMqttServiceSource::capture() const
{
    QVariantMap result{
        {QStringLiteral("connected"), m_client && m_client->isConnected()},
        {QStringLiteral("status"), m_client ? m_client->status() : QStringLiteral("Disconnected")},
        {QStringLiteral("shotServerRunning"), m_shotServer && m_shotServer->isRunning()},
        {QStringLiteral("shotServerUrl"), m_shotServer ? m_shotServer->url() : QString()}
    };
    if (!m_settings || !m_settings->mqtt())
        return result;

    const auto* mqtt = m_settings->mqtt();
    const QString ca = mqtt->mqttCaCertificate();
    result.insert(QStringLiteral("mqttEnabled"), mqtt->mqttEnabled());
    result.insert(QStringLiteral("mqttBrokerHost"), mqtt->mqttBrokerHost());
    result.insert(QStringLiteral("mqttBrokerPort"), mqtt->mqttBrokerPort());
    result.insert(QStringLiteral("mqttUsername"), mqtt->mqttUsername());
    result.insert(QStringLiteral("mqttPasswordConfigured"), !mqtt->mqttPassword().isEmpty());
    result.insert(QStringLiteral("mqttBaseTopic"), mqtt->mqttBaseTopic());
    result.insert(QStringLiteral("mqttPublishInterval"), mqtt->mqttPublishInterval());
    result.insert(QStringLiteral("mqttRetainMessages"), mqtt->mqttRetainMessages());
    result.insert(QStringLiteral("mqttHomeAssistantDiscovery"), mqtt->mqttHomeAssistantDiscovery());
    result.insert(QStringLiteral("mqttClientId"), mqtt->mqttClientId());
    result.insert(QStringLiteral("mqttDeviceId"), mqtt->mqttDeviceId());
    result.insert(QStringLiteral("mqttUseTls"), mqtt->mqttUseTls());
    result.insert(QStringLiteral("mqttCaCertificateConfigured"), !ca.trimmed().isEmpty());
    result.insert(QStringLiteral("mqttCaCertificateSummary"), SettingsMqtt::describeCaCertificate(ca));
    return result;
}

void NativeMqttServiceSource::publish()
{
    const QVariantMap current = capture();
    QByteArray passwordToken;
    if (m_settings && m_settings->mqtt()) {
        passwordToken = QCryptographicHash::hash(m_settings->mqtt()->mqttPassword().toUtf8(),
                                                  QCryptographicHash::Sha256);
    }
    QVariantMap preferences;
    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("mqtt")))
            preferences.insert(it.key(), it.value());
    }
    if (preferences != m_preferenceSnapshot || passwordToken != m_passwordRevisionToken) {
        m_preferenceSnapshot = preferences;
        m_passwordRevisionToken = passwordToken;
        ++m_revision;
    }
    m_snapshot = current;
    setSnapshot(m_snapshot);
    setRevision(m_revision);
    setReady(m_settings && m_settings->mqtt() && m_client);
}

bool NativeMqttServiceSource::setPreference(const QVariantMap& arguments, QString* reason)
{
    if (!hasOnlyKeys(arguments, {QStringLiteral("name"), QStringLiteral("value"),
                                 QStringLiteral("passwordConfirmation"), QStringLiteral("recommendedPort")})) {
        *reason = QStringLiteral("invalidArguments");
        return false;
    }
    if (!m_settings || !m_settings->mqtt()) {
        *reason = QStringLiteral("unavailable");
        return false;
    }
    QString name;
    if (!exactStringArgument(arguments, QStringLiteral("name"), &name)) {
        *reason = QStringLiteral("invalidValue");
        return false;
    }
    if (!MqttServicePolicy::supportedPreference(name)) {
        *reason = QStringLiteral("invalidPreference");
        return false;
    }
    const QVariant value = arguments.value(QStringLiteral("value"));
    if (!MqttServicePolicy::validPreferenceValue(name, value)) {
        *reason = QStringLiteral("invalidValue");
        return false;
    }
    auto* mqtt = m_settings->mqtt();
    bool boolValue = false;
    int intValue = 0;
    QString stringValue;
    if (MqttServicePolicy::isBoolPreference(name)) {
        if (!MqttServicePolicy::exactBool(value, &boolValue)) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
    } else if (name == QLatin1String("mqttBrokerPort")) {
        if (!MqttServicePolicy::boundedInt(value, 1, 65535, &intValue)) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
    } else if (name == QLatin1String("mqttPublishInterval")) {
        if (!MqttServicePolicy::boundedInt(value, 100, 5000, &intValue)
            || (intValue != 100 && intValue != 500 && intValue != 1000 && intValue != 5000)) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
    } else {
        const int limit = name == QLatin1String("mqttCaCertificate") ? 65536
            : name == QLatin1String("mqttPassword") ? 4096 : 256;
        if (!MqttServicePolicy::exactString(value, limit, &stringValue)) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
    }

    QString passwordConfirmation;
    if (!exactStringArgument(arguments, QStringLiteral("passwordConfirmation"),
                             &passwordConfirmation, false)) {
        *reason = QStringLiteral("invalidValue");
        return false;
    }
    if (name == QLatin1String("mqttPassword")) {
        mqtt->setMqttPassword(stringValue);
    } else if (name == QLatin1String("mqttCaCertificate")) {
        if (!stringValue.trimmed().isEmpty()
            && SettingsMqtt::describeCaCertificate(stringValue).isEmpty()) {
            *reason = QStringLiteral("invalidCertificate");
            return false;
        }
        QJsonObject candidate;
        if (mqtt->mqttCaCertificate().trimmed() != stringValue.trimmed())
            candidate.insert(QStringLiteral("mqttCaCertificate"), stringValue.trimmed());
        if (!mqtt->passwordExposingChanges(candidate).isEmpty()
            && passwordConfirmation != mqtt->mqttPassword()) {
            *reason = QStringLiteral("passwordReentryRequired");
            return false;
        }
        mqtt->setMqttCaCertificate(stringValue.trimmed());
    } else if (name == QLatin1String("mqttBrokerHost")
               || name == QLatin1String("mqttBrokerPort")
               || name == QLatin1String("mqttUseTls")) {
        QJsonObject candidate;
        int recommendedPort = 0;
        const bool hasRecommendedPort = arguments.contains(QStringLiteral("recommendedPort"));
        if (hasRecommendedPort) {
            if (name != QLatin1String("mqttUseTls")
                || !MqttServicePolicy::boundedInt(arguments.value(QStringLiteral("recommendedPort")),
                                                  1, 65535, &recommendedPort)
                || (boolValue && recommendedPort != 8883)
                || (!boolValue && recommendedPort != 1883)
                || (mqtt->mqttBrokerPort() != (boolValue ? 1883 : 8883))) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
        }
        if (name == QLatin1String("mqttBrokerHost")) {
            candidate.insert(name, stringValue.trimmed());
        } else if (name == QLatin1String("mqttBrokerPort")) {
            candidate.insert(name, intValue);
        } else {
            candidate.insert(name, boolValue);
        }
        if (hasRecommendedPort)
            candidate.insert(QStringLiteral("mqttBrokerPort"), recommendedPort);
        if (!mqtt->passwordExposingChanges(candidate).isEmpty()
            && passwordConfirmation != mqtt->mqttPassword()) {
            *reason = QStringLiteral("passwordReentryRequired");
            return false;
        }
        if (name == QLatin1String("mqttBrokerHost")) mqtt->setMqttBrokerHost(stringValue.trimmed());
        else if (name == QLatin1String("mqttBrokerPort")) mqtt->setMqttBrokerPort(intValue);
        else if (hasRecommendedPort && boolValue) {
            // Enable TLS before moving to 8883 so the reconnect from changing
            // the port can never send credentials in clear text.
            mqtt->setMqttUseTls(true);
            mqtt->setMqttBrokerPort(recommendedPort);
        } else if (hasRecommendedPort) {
            // Move to 1883 while TLS is still enabled, then disable TLS. The
            // transient reconnect cannot expose the stored password.
            mqtt->setMqttBrokerPort(recommendedPort);
            mqtt->setMqttUseTls(false);
        } else mqtt->setMqttUseTls(boolValue);
    } else if (name == QLatin1String("mqttEnabled")) mqtt->setMqttEnabled(boolValue);
    else if (name == QLatin1String("mqttUsername")) mqtt->setMqttUsername(stringValue);
    else if (name == QLatin1String("mqttBaseTopic")) mqtt->setMqttBaseTopic(stringValue);
    else if (name == QLatin1String("mqttPublishInterval")) mqtt->setMqttPublishInterval(intValue);
    else if (name == QLatin1String("mqttRetainMessages")) mqtt->setMqttRetainMessages(boolValue);
    else if (name == QLatin1String("mqttHomeAssistantDiscovery")) mqtt->setMqttHomeAssistantDiscovery(boolValue);
    else if (name == QLatin1String("mqttClientId")) {
        if (stringValue.trimmed().isEmpty()) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
        mqtt->setMqttClientId(stringValue.trimmed());
    }
    return true;
}

void NativeMqttServiceSource::execute(QString requestId, QString operation,
                                     QVariantMap arguments, qint64 expectedRevision)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        const QString reason = requestStartReason(start);
        QMetaObject::invokeMethod(this, [this, requestId, operation, reason] {
            finish(requestId, operation, false, failure(reason), reason);
        }, Qt::QueuedConnection);
        return;
    }
    if (!m_settings || !m_client || !ready()) {
        finish(requestId, operation, false, failure(QStringLiteral("unavailable")), QStringLiteral("unavailable"));
        return;
    }
    if (!MqttServicePolicy::supportedOperation(operation)) {
        finish(requestId, operation, false, failure(QStringLiteral("invalidOperation")), QStringLiteral("invalidOperation"));
        return;
    }
    if (expectedRevision != m_revision) {
        finish(requestId, operation, false, failure(QStringLiteral("staleSnapshot")), QStringLiteral("staleSnapshot"));
        return;
    }
    if (operation != QLatin1String("setPreference") && !arguments.isEmpty()) {
        const QString reason = QStringLiteral("invalidArguments");
        finish(requestId, operation, false, failure(reason), reason);
        return;
    }

    QString reason;
    if (operation == QLatin1String("setPreference")) {
        if (!setPreference(arguments, &reason)) {
            finish(requestId, operation, false, failure(reason), reason);
            return;
        }
        publish();
        finish(requestId, operation, true, {}, {});
    } else if (operation == QLatin1String("connectToBroker")) {
        if (m_settings->mqtt()->mqttBrokerHost().trimmed().isEmpty()) {
            reason = QStringLiteral("brokerHostRequired");
            finish(requestId, operation, false, failure(reason), reason);
            return;
        }
        m_client->connectToBroker();
        finish(requestId, operation, true, {}, {});
    } else if (operation == QLatin1String("disconnectFromBroker")) {
        m_client->disconnectFromBroker();
        finish(requestId, operation, true, {}, {});
    } else if (operation == QLatin1String("publishDiscovery")) {
        if (!m_client->isConnected()) {
            reason = QStringLiteral("notConnected");
            finish(requestId, operation, false, failure(reason), reason);
            return;
        }
        m_client->publishDiscovery();
        finish(requestId, operation, true, {}, {});
    } else if (operation == QLatin1String("newDeviceId")) {
        m_client->newDeviceId();
        publish();
        finish(requestId, operation, true, {}, {});
    }
}

void NativeMqttServiceSource::finish(const QString& requestId, const QString& operation,
                                    bool success, const QVariantMap& result,
                                    const QString& reason)
{
    m_requests.finish(requestId);
    emit operationCompleted(requestId, operation, success, result, reason, m_revision);
}
