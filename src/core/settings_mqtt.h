#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

class SettingsMqtt : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool mqttEnabled READ mqttEnabled WRITE setMqttEnabled NOTIFY mqttEnabledChanged)
    Q_PROPERTY(QString mqttBrokerHost READ mqttBrokerHost WRITE setMqttBrokerHost NOTIFY mqttBrokerHostChanged)
    Q_PROPERTY(int mqttBrokerPort READ mqttBrokerPort WRITE setMqttBrokerPort NOTIFY mqttBrokerPortChanged)
    Q_PROPERTY(QString mqttUsername READ mqttUsername WRITE setMqttUsername NOTIFY mqttUsernameChanged)
    Q_PROPERTY(QString mqttPassword READ mqttPassword WRITE setMqttPassword NOTIFY mqttPasswordChanged)
    Q_PROPERTY(QString mqttBaseTopic READ mqttBaseTopic WRITE setMqttBaseTopic NOTIFY mqttBaseTopicChanged)
    Q_PROPERTY(int mqttPublishInterval READ mqttPublishInterval WRITE setMqttPublishInterval NOTIFY mqttPublishIntervalChanged)
    Q_PROPERTY(bool mqttRetainMessages READ mqttRetainMessages WRITE setMqttRetainMessages NOTIFY mqttRetainMessagesChanged)
    Q_PROPERTY(bool mqttHomeAssistantDiscovery READ mqttHomeAssistantDiscovery WRITE setMqttHomeAssistantDiscovery NOTIFY mqttHomeAssistantDiscoveryChanged)
    Q_PROPERTY(QString mqttClientId READ mqttClientId WRITE setMqttClientId NOTIFY mqttClientIdChanged)

public:
    explicit SettingsMqtt(QObject* parent = nullptr);

    bool mqttEnabled() const;
    void setMqttEnabled(bool enabled);

    QString mqttBrokerHost() const;
    void setMqttBrokerHost(const QString& host);

    int mqttBrokerPort() const;
    void setMqttBrokerPort(int port);

    QString mqttUsername() const;
    void setMqttUsername(const QString& username);

    QString mqttPassword() const;
    void setMqttPassword(const QString& password);

    QString mqttBaseTopic() const;
    void setMqttBaseTopic(const QString& topic);

    int mqttPublishInterval() const;
    void setMqttPublishInterval(int interval);

    bool mqttRetainMessages() const;
    void setMqttRetainMessages(bool retain);

    bool mqttHomeAssistantDiscovery() const;
    void setMqttHomeAssistantDiscovery(bool enabled);

    QString mqttClientId() const;
    void setMqttClientId(const QString& clientId);

signals:
    void mqttEnabledChanged();
    void mqttBrokerHostChanged();
    void mqttBrokerPortChanged();
    void mqttUsernameChanged();
    void mqttPasswordChanged();
    void mqttBaseTopicChanged();
    void mqttPublishIntervalChanged();
    void mqttRetainMessagesChanged();
    void mqttHomeAssistantDiscoveryChanged();
    void mqttClientIdChanged();

private:
    mutable QSettings m_settings;
};
