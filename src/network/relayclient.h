#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>

class DE1Device;
class MachineState;
class Settings;

class RelayClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit RelayClient(DE1Device* device, MachineState* machineState,
                         Settings* settings, QObject* parent = nullptr);

    bool isConnected() const;
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

signals:
    void connectedChanged();
    void enabledChanged();

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onStateChanged();
    void onReconnectTimer();
    void onPingTimer();

private:
    void connectToRelay();
    void handleCommand(const QString& commandId, const QString& command);
    void pushStatus();
    QJsonObject buildStatusJson() const;

    QWebSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_pingTimer;
    QTimer m_statusPushTimer;
    DE1Device* m_device;
    MachineState* m_machineState;
    Settings* m_settings;
    bool m_enabled = false;
    int m_reconnectAttempts = 0;
    QString m_lastStatusJson; // Deduplicate status pushes
};
