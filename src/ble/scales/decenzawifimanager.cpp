#include "decenzawifimanager.h"

#include "decenzaprovisioningclient.h"
#include "../blemanager.h"
#include "../scaledevice.h"
#include "../../core/settings_connections.h"

#include <QBluetoothDeviceInfo>

DecenzaWifiManager::DecenzaWifiManager(BLEManager* bleManager,
                                       SettingsConnections* connections,
                                       QObject* parent)
    : QObject(parent)
    , m_bleManager(bleManager)
    , m_connections(connections)
{
    if (m_connections) {
        connect(m_connections, &SettingsConnections::scaleWifiPairingsChanged,
                this, &DecenzaWifiManager::pairingsChanged);
    }
}

QVariantMap DecenzaWifiManager::pairings() const {
    return m_connections ? m_connections->scaleWifiPairings() : QVariantMap();
}

void DecenzaWifiManager::provisionWifi(const QString& address,
                                       const QString& ssid,
                                       const QString& passphrase) {
    if (m_active) {
        emit provisioningFailed(QStringLiteral("Provisioning already in progress"));
        return;
    }
    if (!m_bleManager) {
        emit provisioningFailed(QStringLiteral("BLE manager not available"));
        return;
    }
    if (ssid.isEmpty()) {
        emit provisioningFailed(QStringLiteral("SSID is required"));
        return;
    }

    QBluetoothDeviceInfo device = m_bleManager->getScaleDeviceInfo(address);
    if (device.address().isNull()) {
        // The scale isn't in the current scan-results buffer. Most common
        // cause: the user is *already connected* to this scale over BLE,
        // so it didn't re-emerge in the new scan we kicked when the wizard
        // opened. Fall back to constructing a device info from the MAC
        // alone — the BLE controller only needs the address to connect,
        // and on Android/desktop QBluetoothAddress(QString) accepts the
        // standard colon-separated MAC format. iOS cannot construct this
        // way (no exposed MACs), so it stays on the existing failure path.
#ifdef Q_OS_IOS
        emit provisioningFailed(QStringLiteral("Scale not found in scan results — scan again"));
        return;
#else
        const QBluetoothAddress macAddress(address);
        if (macAddress.isNull()) {
            emit provisioningFailed(QStringLiteral("Invalid scale address: %1").arg(address));
            return;
        }
        device = QBluetoothDeviceInfo(macAddress, QString(), 0);
#endif
    }

    m_pendingMac = address.toLower();
    setPhase(QStringLiteral("bleConnecting"));

    m_active = new DecenzaProvisioningClient(device, this);

    connect(m_active, &DecenzaProvisioningClient::statusUpdate, this,
            [this](int state, const QString& ip, quint8 /*err*/) {
                // STATUS arriving means we've already gotten past BLE/discovery
                // and are now in the Wi-Fi association phase, until we hit a
                // terminal state. Translate the firmware's state byte into a
                // user-facing phase string.
                if (state == static_cast<int>(DecenzaProvisioningClient::State::Connecting)) {
                    setPhase(QStringLiteral("wifiConnecting"));
                } else if (state == static_cast<int>(DecenzaProvisioningClient::State::Connected)) {
                    setPhase(QStringLiteral("wifiConnecting"), ip);
                }
                // Connected/Failed terminals are handled by the dedicated signals.
            });

    connect(m_active, &DecenzaProvisioningClient::provisioningCompleted, this,
            [this](const QString& ip) {
                if (m_connections) {
                    m_connections->setScaleWifiPairing(m_pendingMac, ip, 8765);
                }
                const QString mac = m_pendingMac;
                m_pendingMac.clear();
                m_active = nullptr;
                setPhase(QStringLiteral("succeeded"), ip);
                emit provisioningCompleted(mac, ip);

                // Auto-switch the active session to Wi-Fi: disconnect the
                // BLE link the user is currently on. The reconnect loop in
                // main.cpp picks up scaleReconnectAttempt==0 + the freshly
                // saved pairing → factory builds a WifiScaleTransport on
                // the retry → user sees the badge flip from BLE to Wi-Fi
                // within ~2 s, no app restart required. Spec note: this
                // is a deliberate session boundary triggered by explicit
                // user action (the wizard), not the auto-fallback case
                // that's prohibited by design.md Decision 7.
                if (m_bleManager && m_bleManager->scaleDevice()
                    && m_bleManager->scaleDevice()->isConnected()) {
                    m_bleManager->scaleDevice()->disconnectFromScale();
                }
            });

    connect(m_active, &DecenzaProvisioningClient::provisioningFailed, this,
            [this](const QString& reason) {
                m_pendingMac.clear();
                m_active = nullptr;
                setPhase(QStringLiteral("failed"), QString(), reason);
                emit provisioningFailed(reason);
            });

    // Forward log lines for the BLE log overlay.
    connect(m_active, &DecenzaProvisioningClient::logMessage,
            m_bleManager, &BLEManager::appendScaleLog);

    // After fee1/fee2/fee3 writes the client transitions to "waiting on
    // STATUS" silently — the UI shows "writing" for that bridge instant.
    // Keep "writing" visible until the first STATUS notification flips it.
    setPhase(QStringLiteral("writing"));

    m_active->provisionWifi(ssid, passphrase);
}

void DecenzaWifiManager::forgetWifi(const QString& mac) {
    if (!m_connections) {
        emit forgetFailed(QStringLiteral("Settings not available"));
        return;
    }
    const QString lower = mac.toLower();

    // Always clear locally, even if the remote forget fails — the user's
    // intent is "stop using this pairing", and the BLE round-trip is a
    // best-effort NVS clear on the scale.
    m_connections->clearScaleWifiPairing(lower);
    emit forgetCompleted(lower);

    // Best-effort: tell the scale to clear its NVS too. Only attempt if the
    // scale is currently within BLE range (i.e. visible in the BLEManager
    // scan results); otherwise the user has already removed the pairing
    // locally, which is the important part.
    if (!m_bleManager) return;
    const QBluetoothDeviceInfo device = m_bleManager->getScaleDeviceInfo(lower);
    if (device.address().isNull()) return;

    auto* forgetClient = new DecenzaProvisioningClient(device, this);
    connect(forgetClient, &DecenzaProvisioningClient::logMessage,
            m_bleManager, &BLEManager::appendScaleLog);
    forgetClient->forgetWifi();
}

void DecenzaWifiManager::clearPhase() {
    setPhase(QString());
}

void DecenzaWifiManager::refreshStoredIp(const QString& address) {
    if (!m_bleManager || !m_connections) return;
    const QBluetoothDeviceInfo device = m_bleManager->getScaleDeviceInfo(address);
    if (device.address().isNull()) return;
    const QString mac = address.toLower();

    auto* client = new DecenzaProvisioningClient(device, this);
    connect(client, &DecenzaProvisioningClient::logMessage,
            m_bleManager, &BLEManager::appendScaleLog);
    connect(client, &DecenzaProvisioningClient::statusRefreshed, this,
        [this, mac](bool ok, int /*state*/, const QString& ip, quint8 /*err*/) {
            if (!ok || ip.isEmpty()) return;
            const QVariantMap existing = m_connections->scaleWifiPairing(mac);
            const QString existingIp = existing.value(QStringLiteral("ip")).toString();
            if (existingIp == ip) return;  // No change — skip the write.
            const int port = existing.value(QStringLiteral("port"), 8765).toInt();
            m_connections->setScaleWifiPairing(mac, ip, port);
        });
    client->refreshStatus();
}

void DecenzaWifiManager::setPhase(const QString& phase, const QString& ip,
                                  const QString& message) {
    if (m_phase == phase && m_ip == ip && m_message == message) return;
    m_phase = phase;
    m_ip = ip;
    m_message = message;
    emit provisionPhaseChanged();
}
