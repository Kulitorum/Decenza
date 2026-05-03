#pragma once

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QObject>
#include <QString>
#include <functional>

class QTimer;

// Drives the Decenza Scale's BLE Wi-Fi provisioning service
// (0000feed-decc-1000-8000-00805f9b34fb). Single-shot: each instance handles
// exactly one provisioning, forget, or status-read operation, then
// deleteLater()s itself after emitting its terminal signal.
//
// Always uses a dedicated short-lived QLowEnergyController separate from the
// runtime scale transport — see Decision 2 in the change's design.md for
// rationale. Specifically, this lets the user provision a scale that is
// *not* currently the active runtime scale (e.g. provisioning a second
// scale on the bench, or re-provisioning after a Wi-Fi network change),
// without disrupting the active connection.
//
// Wire protocol (per the DecenzaScale firmware repo):
//   fee1 (write,  ≤32 B): UTF-8 SSID
//   fee2 (write,  ≤64 B): UTF-8 passphrase
//   fee3 (write,    1 B): 0x01 connect, 0x02 forget, 0x03 reconnect
//   fee4 (notify,   7 B): [state, rssi_int8, ip0, ip1, ip2, ip3, err]
//     state: 0=Idle, 1=Connecting, 2=Connected, 3=Failed
class DecenzaProvisioningClient : public QObject {
    Q_OBJECT

public:
    // Service / characteristic UUIDs. Public so tests and the runtime
    // transport's IP-refresh helper can reference them by name.
    static const QBluetoothUuid kServiceUuid;
    static const QBluetoothUuid kSsidCharUuid;
    static const QBluetoothUuid kPassCharUuid;
    static const QBluetoothUuid kControlCharUuid;
    static const QBluetoothUuid kStatusCharUuid;

    enum class State : quint8 {
        Idle = 0,
        Connecting = 1,
        Connected = 2,
        Failed = 3
    };
    Q_ENUM(State)

    explicit DecenzaProvisioningClient(const QBluetoothDeviceInfo& device,
                                       QObject* parent = nullptr);
    ~DecenzaProvisioningClient() override;

    // Kick off a provisioning session: connect → subscribe fee4 → write
    // SSID → write passphrase → write 0x01 to fee3 → wait for STATUS to
    // settle on Connected or Failed. Emits provisioningCompleted on success
    // (state==Connected) carrying the dotted-decimal IP, or
    // provisioningFailed with a reason string. Self-deletes after either.
    void provisionWifi(const QString& ssid, const QString& passphrase);

    // Tell the scale to forget its stored Wi-Fi credentials (NVS clear).
    // Connect → discover → write 0x02 to fee3 → emit forgetCompleted. The
    // scale itself does not echo a confirmation, so success here means
    // "the write reached the controller" — the caller is responsible for
    // also clearing the matching pairing in Settings.connections.
    void forgetWifi();

    // Read fee4 STATUS once and report the parsed reading. Used by the
    // runtime BLE path's opportunistic IP-refresh: after a successful BLE
    // connect to a Decenza scale, this spawns a separate short-lived
    // controller, reads STATUS, and emits statusRefreshed exactly once.
    // The instance self-deletes after the terminal signal.
    void refreshStatus();

    // One-shot STATUS read against an *already-discovered* provisioning
    // service. Used by the runtime BLE transport's opportunistic IP-refresh
    // path (Phase 3) — at that point the BLE controller and service are
    // already alive on the active connection, so spinning up another
    // controller would be wasteful. The caller owns the service; this
    // helper just issues the read and forwards the parsed result.
    //
    // Callback signature: (success, state, dottedDecimalIp, err)
    static void readWifiStatusOnce(
        QLowEnergyService* provisioningService,
        std::function<void(bool, State, QString, quint8)> callback);

    // Parse a 7-byte STATUS payload. Public for unit testing — protocol
    // parsing should be exercisable without spinning up BLE.
    struct StatusReading {
        State state;
        qint8 rssi;
        QString ip;     // Dotted-decimal "a.b.c.d", empty if state != Connected.
        quint8 err;
    };
    static StatusReading parseStatus(const QByteArray& data);

signals:
    // Terminal signals — exactly one is emitted per instance, then the
    // instance self-deletes via deleteLater().
    void provisioningCompleted(const QString& ip);
    void provisioningFailed(const QString& reason);
    void forgetCompleted();
    void forgetFailed(const QString& reason);
    // Emitted by refreshStatus(). `ok=true` iff the STATUS read succeeded
    // and decoded; the other fields carry the reading regardless.
    void statusRefreshed(bool ok, int state, const QString& ip, quint8 err);

    // Live STATUS updates emitted while the provisioning state machine is
    // still resolving — useful for the UI to show "Connecting…" before the
    // terminal signal lands. Not emitted for forgetWifi().
    void statusUpdate(int state, const QString& ip, quint8 err);

    // Streamed log lines for the BLE log overlay.
    void logMessage(const QString& message);

private:
    void log(const QString& message);
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error err);
    void onServiceDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c,
                                 const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& c,
                                 const QByteArray& value);
    void onTerminalTimeout();

    void writeProvisioningSequence();
    void finishSuccess(const QString& ip);
    void finishFailure(const QString& reason);
    void finishForgetSuccess();
    void finishForgetFailure(const QString& reason);
    void finishRefresh(bool ok, State state, const QString& ip, quint8 err);

    enum class Mode { None, Provision, Forget, Refresh };
    enum class WriteStep { None, Ssid, Pass, Control };

    QBluetoothDeviceInfo m_device;
    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;
    QTimer* m_terminalTimer = nullptr;

    Mode m_mode = Mode::None;
    WriteStep m_step = WriteStep::None;
    QString m_ssid;
    QString m_passphrase;
    bool m_finished = false;  // Guards against double-emit + self-delete.

    // Watchdog: if no terminal STATUS arrives within this window, give up.
    static constexpr int kProvisionTimeoutMs = 30000;
    static constexpr int kForgetTimeoutMs = 5000;
    static constexpr int kRefreshTimeoutMs = 5000;
};
