#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QJsonObject>
#include <memory>

class DE1Device;
class MachineState;
class Settings;
class ScreenCaptureService;
class QQuickWindow;

class RelayClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit RelayClient(DE1Device* device, MachineState* machineState,
                         Settings* settings, QObject* parent = nullptr);
    ~RelayClient();

    bool isConnected() const;
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void setWindow(QQuickWindow* window);
    void shutdown();

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
    void onBinaryMessageReceived(const QByteArray& data);
    void onRemoteActivityTimeout();

private:
    void connectToRelay();
    void handleCommand(const QString& commandId, const QString& command);
    void pushStatus();
    QJsonObject buildStatusJson() const;
    // Restart the inactivity watchdog whenever the phone sends any traffic.
    // The tablet uses a time-based fallback here because the AWS relay proxy
    // keeps the tablet↔AWS socket alive (5-min ping) after the phone dies, so
    // onDisconnected() never fires for the phone's disconnection. The correct
    // long-term fix is a relay-server peer_disconnected event; until that
    // exists, the watchdog timer is the only available signal.
    void noteRemoteActivity();

    QWebSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_pingTimer;
    QTimer m_statusPushTimer;
    QTimer m_remoteActivityTimer;
    DE1Device* m_device;
    MachineState* m_machineState;
    Settings* m_settings;
    bool m_enabled = false;
    int m_reconnectAttempts = 0;
    QString m_lastStatusJson; // Deduplicate status pushes
    QQuickWindow* m_window = nullptr;
    std::unique_ptr<ScreenCaptureService> m_captureService;
};
