#include "settings_mqtt.h"

SettingsMqtt::SettingsMqtt(QObject* parent)
    : QObject(parent)
    , m_settings("DecentEspresso", "DE1Qt")
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
