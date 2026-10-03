#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>
#include <QStringList>

class QJsonObject;

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
    Q_PROPERTY(QString mqttDeviceId READ mqttDeviceId NOTIFY mqttDeviceIdChanged FINAL)

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

    // Keys in `changes` (settings-key -> new value) that would send the stored password to
    // a broker the user has not authenticated, or in the clear: a different host or port,
    // TLS turned off, a different CA. Empty when no password is stored. The web settings
    // page asks for the password again before applying these; MCP, which cannot supply
    // it, refuses them.
    QStringList passwordExposingChanges(const QJsonObject& changes) const;

    // Home Assistant identity: builds every entity unique_id and the device identifier.
    // Unlike the client ID (the broker's session key, which must be unique per install),
    // it travels with backup and migration so a replacement tablet stays the same device.
    QString mqttDeviceId() const;
    // A fresh random ID, and the move to device discovery marked done: a new identity has
    // no per-entity topics of its own. Clears nothing on the broker — the previous ID may
    // belong to another install restored from the same backup.
    void regenerateMqttDeviceId();
    // Once per install, before the first connect of this version: the device ID takes the
    // stored client ID (what built this install's unique_ids), and the client ID becomes a
    // fresh one, which also separates installs that a restored backup gave the same ID.
    void ensureMqttIdentity();
    // Imports a device ID from a backup; also the path for a pre-change backup's client ID.
    void importMqttDeviceId(const QString& deviceId);
    static QString newMqttId();

    // Bookkeeping, not user settings; no property and no signal — nothing displays them.
    // Per-entity discovery topics published by builds before device discovery: read once
    // by the migration, then cleared.
    QStringList mqttPublishedDiscoveryTopics() const;
    void setMqttPublishedDiscoveryTopics(const QStringList& topics);
    // The device message's components as last published ("<objectId>=<platform>"), so one
    // that leaves the set can be sent once with only its platform, as Home Assistant requires.
    QStringList mqttPublishedDiscoveryComponents() const;
    void setMqttPublishedDiscoveryComponents(const QStringList& components);
    bool mqttDiscoveryMigrated() const;
    void setMqttDiscoveryMigrated(bool migrated);

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
    void mqttDeviceIdChanged();

private:
    mutable AppSettings m_settings;
};
