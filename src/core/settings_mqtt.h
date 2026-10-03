#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>
#include <QStringList>

class SettingsMqtt : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool mqttEnabled READ mqttEnabled WRITE setMqttEnabled NOTIFY mqttEnabledChanged FINAL)
    Q_PROPERTY(QString mqttBrokerHost READ mqttBrokerHost WRITE setMqttBrokerHost NOTIFY mqttBrokerHostChanged FINAL)
    Q_PROPERTY(int mqttBrokerPort READ mqttBrokerPort WRITE setMqttBrokerPort NOTIFY mqttBrokerPortChanged FINAL)
    Q_PROPERTY(QString mqttUsername READ mqttUsername WRITE setMqttUsername NOTIFY mqttUsernameChanged FINAL)
    Q_PROPERTY(QString mqttPassword READ mqttPassword WRITE setMqttPassword NOTIFY mqttPasswordChanged FINAL)
    Q_PROPERTY(QString mqttBaseTopic READ mqttBaseTopic WRITE setMqttBaseTopic NOTIFY mqttBaseTopicChanged FINAL)
    Q_PROPERTY(int mqttPublishInterval READ mqttPublishInterval WRITE setMqttPublishInterval NOTIFY mqttPublishIntervalChanged FINAL)
    Q_PROPERTY(bool mqttRetainMessages READ mqttRetainMessages WRITE setMqttRetainMessages NOTIFY mqttRetainMessagesChanged FINAL)
    Q_PROPERTY(bool mqttHomeAssistantDiscovery READ mqttHomeAssistantDiscovery WRITE setMqttHomeAssistantDiscovery NOTIFY mqttHomeAssistantDiscoveryChanged FINAL)
    Q_PROPERTY(QString mqttClientId READ mqttClientId WRITE setMqttClientId NOTIFY mqttClientIdChanged FINAL)
    Q_PROPERTY(bool mqttUseTls READ mqttUseTls WRITE setMqttUseTls NOTIFY mqttUseTlsChanged FINAL)
    Q_PROPERTY(QString mqttCaCertificate READ mqttCaCertificate WRITE setMqttCaCertificate NOTIFY mqttCaCertificateChanged FINAL)

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

    bool mqttUseTls() const;
    void setMqttUseTls(bool useTls);

    // PEM text of a CA the user trusts in addition to the platform's, for brokers with
    // self-signed certificates. Empty = platform CAs only.
    QString mqttCaCertificate() const;
    void setMqttCaCertificate(const QString& pem);
    // "<subject>, expires <date>" for a PEM CA certificate, or empty when it is not one.
    // The one validity check for every surface that sets mqttCaCertificate.
    static QString describeCaCertificate(const QString& pem);

    // Bookkeeping, not a user setting: the Home Assistant discovery config topics last
    // published, so ones that leave the set (or all, when discovery is turned off) can be
    // cleared from the broker. No property and no signal — nothing displays it.
    QStringList mqttPublishedDiscoveryTopics() const;
    void setMqttPublishedDiscoveryTopics(const QStringList& topics);

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
    void mqttUseTlsChanged();
    void mqttCaCertificateChanged();

private:
    mutable AppSettings m_settings;
};
