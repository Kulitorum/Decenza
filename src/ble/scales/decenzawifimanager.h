#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class BLEManager;
class SettingsConnections;
class DecenzaProvisioningClient;

// QML-facing facade for Decenza Scale Wi-Fi provisioning. Mediates between
// the dialog (QML) and the lower-level DecenzaProvisioningClient. Owns no
// business logic — it just resolves a scale's BLE address to a
// QBluetoothDeviceInfo via BLEManager, drives one provisioning operation
// at a time, and writes/clears the persistent pairing in
// Settings.connections on success.
//
// Constructed once in main.cpp and exposed to QML as a context property
// named "DecenzaWifiManager".
class DecenzaWifiManager : public QObject {
    Q_OBJECT

    // Phase strings consumed directly by QML for status text:
    //   ""               -> idle (no operation in progress)
    //   "bleConnecting"  -> opening BLE link to the scale
    //   "writing"        -> writing SSID/passphrase/control to fee1/fee2/fee3
    //   "wifiConnecting" -> firmware is attempting STA association
    //   "succeeded"      -> terminal: state==Connected, IP captured
    //   "failed"         -> terminal: provisioning failed (see m_message)
    Q_PROPERTY(QString provisionPhase READ provisionPhase NOTIFY provisionPhaseChanged)
    Q_PROPERTY(QString provisionIp READ provisionIp NOTIFY provisionPhaseChanged)
    Q_PROPERTY(QString provisionMessage READ provisionMessage NOTIFY provisionPhaseChanged)
    Q_PROPERTY(QVariantMap pairings READ pairings NOTIFY pairingsChanged)

public:
    explicit DecenzaWifiManager(BLEManager* bleManager,
                                SettingsConnections* connections,
                                QObject* parent = nullptr);

    QString provisionPhase() const { return m_phase; }
    QString provisionIp() const { return m_ip; }
    QString provisionMessage() const { return m_message; }
    QVariantMap pairings() const;

    // Start a provisioning session for the scale at the given BLE address.
    // The manager must be idle — concurrent operations are rejected with a
    // failed signal.
    Q_INVOKABLE void provisionWifi(const QString& address,
                                   const QString& ssid,
                                   const QString& passphrase);

    // Tell the scale at the given MAC to forget its stored Wi-Fi creds and
    // remove the matching pairing locally. The MAC may be either the BLE
    // address (preferred) or a stored pairing key.
    Q_INVOKABLE void forgetWifi(const QString& mac);

    // Reset the manager to idle state without touching any scale or
    // pairing. UI calls this after observing a terminal phase so the next
    // provision attempt starts from a clean slate.
    Q_INVOKABLE void clearPhase();

    // Fire-and-forget BLE STATUS read. Spawns a short-lived
    // DecenzaProvisioningClient in Refresh mode; if the read returns a
    // Connected state with a fresh IP, the matching pairing is updated.
    // Used by the runtime path after a successful BLE connect — see
    // Phase 3.5 of the change. Safe to call when no pairing exists; in
    // that case the result is just discarded silently. Never blocks.
    void refreshStoredIp(const QString& address);

signals:
    void provisionPhaseChanged();
    void pairingsChanged();
    void provisioningCompleted(const QString& mac, const QString& ip);
    void provisioningFailed(const QString& reason);
    void forgetCompleted(const QString& mac);
    void forgetFailed(const QString& reason);

private:
    void setPhase(const QString& phase, const QString& ip = QString(),
                  const QString& message = QString());

    BLEManager* m_bleManager = nullptr;
    SettingsConnections* m_connections = nullptr;
    DecenzaProvisioningClient* m_active = nullptr;
    QString m_pendingMac;  // Lowercase BLE MAC of the in-progress operation.

    QString m_phase;
    QString m_ip;
    QString m_message;
};
