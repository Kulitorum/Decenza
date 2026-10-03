#include "settings_mqtt.h"
#include "settings.h"

#include <QDate>
#include <QJsonObject>
#include <QUuid>
#include <QtNetwork/qtnetworkglobal.h>   // defines the ssl feature for QT_CONFIG
#if QT_CONFIG(ssl)
#include <QSslCertificate>
#endif

SettingsMqtt::SettingsMqtt(QObject* parent)
    : QObject(parent)
{
}

bool SettingsMqtt::mqttEnabled() const {
    return m_settings.value("mqtt/enabled", false).toBool();
}

void SettingsMqtt::setMqttEnabled(bool enabled) {
    if (mqttEnabled() != enabled) {
        m_settings.setValue("mqtt/enabled", enabled);
        emit mqttEnabledChanged();
    }
}

QString SettingsMqtt::mqttBrokerHost() const {
    return m_settings.value("mqtt/brokerHost", "").toString();
}

void SettingsMqtt::setMqttBrokerHost(const QString& host) {
    if (mqttBrokerHost() != host) {
        m_settings.setValue("mqtt/brokerHost", host);
        emit mqttBrokerHostChanged();
    }
}

int SettingsMqtt::mqttBrokerPort() const {
    return m_settings.value("mqtt/brokerPort", 1883).toInt();
}

void SettingsMqtt::setMqttBrokerPort(int port) {
    if (mqttBrokerPort() != port) {
        m_settings.setValue("mqtt/brokerPort", port);
        emit mqttBrokerPortChanged();
    }
}

QString SettingsMqtt::mqttUsername() const {
    return m_settings.value("mqtt/username", "").toString();
}

void SettingsMqtt::setMqttUsername(const QString& username) {
    if (mqttUsername() != username) {
        m_settings.setValue("mqtt/username", username);
        emit mqttUsernameChanged();
    }
}

QString SettingsMqtt::mqttPassword() const {
    return m_settings.value("mqtt/password", "").toString();
}

void SettingsMqtt::setMqttPassword(const QString& password) {
    if (mqttPassword() != password) {
        m_settings.setValue("mqtt/password", password);
        emit mqttPasswordChanged();
    }
}

QString SettingsMqtt::mqttBaseTopic() const {
    return m_settings.value("mqtt/baseTopic", "decenza").toString();
}

void SettingsMqtt::setMqttBaseTopic(const QString& topic) {
    if (mqttBaseTopic() != topic) {
        m_settings.setValue("mqtt/baseTopic", topic);
        emit mqttBaseTopicChanged();
    }
}

int SettingsMqtt::mqttPublishInterval() const {
    return m_settings.value("mqtt/publishInterval", 1000).toInt();
}

void SettingsMqtt::setMqttPublishInterval(int interval) {
    if (mqttPublishInterval() != interval) {
        m_settings.setValue("mqtt/publishInterval", interval);
        emit mqttPublishIntervalChanged();
    }
}

bool SettingsMqtt::mqttRetainMessages() const {
    return m_settings.value("mqtt/retainMessages", true).toBool();
}

void SettingsMqtt::setMqttRetainMessages(bool retain) {
    if (mqttRetainMessages() != retain) {
        m_settings.setValue("mqtt/retainMessages", retain);
        emit mqttRetainMessagesChanged();
    }
}

bool SettingsMqtt::mqttHomeAssistantDiscovery() const {
    return m_settings.value("mqtt/homeAssistantDiscovery", true).toBool();
}

void SettingsMqtt::setMqttHomeAssistantDiscovery(bool enabled) {
    if (mqttHomeAssistantDiscovery() != enabled) {
        m_settings.setValue("mqtt/homeAssistantDiscovery", enabled);
        emit mqttHomeAssistantDiscoveryChanged();
    }
}

QString SettingsMqtt::mqttClientId() const {
    return m_settings.value("mqtt/clientId", "").toString();
}

void SettingsMqtt::setMqttClientId(const QString& clientId) {
    if (mqttClientId() != clientId) {
        m_settings.setValue("mqtt/clientId", clientId);
        emit mqttClientIdChanged();
    }
}

bool SettingsMqtt::mqttUseTls() const {
    return m_settings.value("mqtt/useTls", false).toBool();
}

void SettingsMqtt::setMqttUseTls(bool useTls) {
    if (mqttUseTls() != useTls) {
        m_settings.setValue("mqtt/useTls", useTls);
        emit mqttUseTlsChanged();
    }
}

QString SettingsMqtt::mqttCaCertificate() const {
    return m_settings.value("mqtt/caCertificate", "").toString();
}

void SettingsMqtt::setMqttCaCertificate(const QString& pem) {
    if (mqttCaCertificate() != pem) {
        m_settings.setValue("mqtt/caCertificate", pem);
        emit mqttCaCertificateChanged();
    }
}

QStringList SettingsMqtt::mqttPublishedDiscoveryTopics() const {
    return m_settings.value("mqtt/publishedDiscoveryTopics").toStringList();
}

void SettingsMqtt::setMqttPublishedDiscoveryTopics(const QStringList& topics) {
    m_settings.setValue("mqtt/publishedDiscoveryTopics", topics);
}

QString SettingsMqtt::describeCaCertificate(const QString& pem)
{
#if QT_CONFIG(ssl)
    const QList<QSslCertificate> certs = QSslCertificate::fromData(pem.trimmed().toUtf8(), QSsl::Pem);
    if (certs.isEmpty() || certs.first().isNull())
        return {};
    const QSslCertificate& cert = certs.first();
    QString subject = cert.subjectInfo(QSslCertificate::CommonName).join(", ");
    if (subject.isEmpty())
        subject = cert.subjectInfo(QSslCertificate::Organization).join(", ");
    return QStringLiteral("%1, expires %2").arg(subject, cert.expiryDate().date().toString(Qt::ISODate));
#else
    Q_UNUSED(pem);
    return {};
#endif
}

QStringList SettingsMqtt::passwordExposingChanges(const QJsonObject& changes) const
{
    QStringList keys;
    if (mqttPassword().isEmpty())
        return keys;
    if (changes.contains("mqttBrokerHost") && changes.value("mqttBrokerHost").toString() != mqttBrokerHost())
        keys << QStringLiteral("mqttBrokerHost");
    if (changes.contains("mqttBrokerPort") && changes.value("mqttBrokerPort").toInt() != mqttBrokerPort())
        keys << QStringLiteral("mqttBrokerPort");
    if (changes.contains("mqttUseTls") && !changes.value("mqttUseTls").toBool() && mqttUseTls())
        keys << QStringLiteral("mqttUseTls");
    if (changes.contains("mqttCaCertificate")
        && changes.value("mqttCaCertificate").toString().trimmed() != mqttCaCertificate())
        keys << QStringLiteral("mqttCaCertificate");
    return keys;
}

QString SettingsMqtt::newMqttId() {
    // [a-zA-Z0-9_] only: the device ID becomes a discovery topic level, which Home
    // Assistant restricts to [a-zA-Z0-9_-].
    return QStringLiteral("decenza_") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

QString SettingsMqtt::mqttDeviceId() const {
    return m_settings.value("mqtt/deviceId", "").toString();
}

void SettingsMqtt::importMqttDeviceId(const QString& deviceId) {
    if (deviceId.isEmpty() || deviceId == mqttDeviceId())
        return;
    m_settings.setValue("mqtt/deviceId", deviceId);
    // The adopted identity may still have per-entity topics from an earlier version.
    // Moving them again when there are none is harmless: Home Assistant ignores a
    // migrate message for a topic it holds nothing on.
    setMqttDiscoveryMigrated(false);
    setMqttPublishedDiscoveryComponents({});
    emit mqttDeviceIdChanged();
}

void SettingsMqtt::regenerateMqttDeviceId() {
    m_settings.setValue("mqtt/deviceId", newMqttId());
    setMqttDiscoveryMigrated(true);
    setMqttPublishedDiscoveryTopics({});
    setMqttPublishedDiscoveryComponents({});
    emit mqttDeviceIdChanged();
}

void SettingsMqtt::ensureMqttIdentity() {
    if (!mqttDeviceId().isEmpty())
        return;
    const QString previousClientId = mqttClientId();
    const bool fresh = previousClientId.isEmpty();
    m_settings.setValue("mqtt/deviceId", fresh ? newMqttId() : previousClientId);
    // A fresh install has nothing to migrate; an upgraded one may have per-entity topics.
    if (fresh)
        setMqttDiscoveryMigrated(true);
    setMqttClientId(newMqttId());
    emit mqttDeviceIdChanged();
}

QStringList SettingsMqtt::mqttPublishedDiscoveryComponents() const {
    return m_settings.value("mqtt/publishedDiscoveryComponents").toStringList();
}

void SettingsMqtt::setMqttPublishedDiscoveryComponents(const QStringList& components) {
    m_settings.setValue("mqtt/publishedDiscoveryComponents", components);
}

bool SettingsMqtt::mqttDiscoveryMigrated() const {
    return m_settings.value("mqtt/discoveryMigrated", false).toBool();
}

void SettingsMqtt::setMqttDiscoveryMigrated(bool migrated) {
    m_settings.setValue("mqtt/discoveryMigrated", migrated);
}
