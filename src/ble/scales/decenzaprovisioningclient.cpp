#include "decenzaprovisioningclient.h"

#include <QDebug>
#include <QTimer>

namespace {
constexpr quint8 kCmdConnect = 0x01;
constexpr quint8 kCmdForget = 0x02;
}

const QBluetoothUuid DecenzaProvisioningClient::kServiceUuid{
    QStringLiteral("0000feed-decc-1000-8000-00805f9b34fb")};
const QBluetoothUuid DecenzaProvisioningClient::kSsidCharUuid{
    QStringLiteral("0000fee1-decc-1000-8000-00805f9b34fb")};
const QBluetoothUuid DecenzaProvisioningClient::kPassCharUuid{
    QStringLiteral("0000fee2-decc-1000-8000-00805f9b34fb")};
const QBluetoothUuid DecenzaProvisioningClient::kControlCharUuid{
    QStringLiteral("0000fee3-decc-1000-8000-00805f9b34fb")};
const QBluetoothUuid DecenzaProvisioningClient::kStatusCharUuid{
    QStringLiteral("0000fee4-decc-1000-8000-00805f9b34fb")};

DecenzaProvisioningClient::DecenzaProvisioningClient(
    const QBluetoothDeviceInfo& device, QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_terminalTimer(new QTimer(this))
{
    m_terminalTimer->setSingleShot(true);
    connect(m_terminalTimer, &QTimer::timeout,
            this, &DecenzaProvisioningClient::onTerminalTimeout);
}

DecenzaProvisioningClient::~DecenzaProvisioningClient() {
    if (m_controller) {
        m_controller->disconnect();
        if (m_controller->state() != QLowEnergyController::UnconnectedState) {
            m_controller->disconnectFromDevice();
        }
        m_controller->deleteLater();
        m_controller = nullptr;
    }
}

void DecenzaProvisioningClient::log(const QString& message) {
    const QString line = QStringLiteral("[wifi/provisioning] ") + message;
    qDebug().noquote() << line;
    emit logMessage(line);
}

void DecenzaProvisioningClient::provisionWifi(const QString& ssid,
                                              const QString& passphrase) {
    if (m_mode != Mode::None) {
        finishFailure(QStringLiteral("Provisioning already in progress"));
        return;
    }
    m_mode = Mode::Provision;
    m_ssid = ssid;
    m_passphrase = passphrase;

    log(QStringLiteral("provisionWifi() ssid=%1").arg(ssid));
    m_terminalTimer->start(kProvisionTimeoutMs);

    m_controller = QLowEnergyController::createCentral(m_device, this);
    if (!m_controller) {
        finishFailure(QStringLiteral("Failed to create BLE controller"));
        return;
    }
    connect(m_controller, &QLowEnergyController::connected,
            this, &DecenzaProvisioningClient::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &DecenzaProvisioningClient::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &DecenzaProvisioningClient::onControllerError);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &DecenzaProvisioningClient::onServiceDiscoveryFinished);

    m_controller->connectToDevice();
}

void DecenzaProvisioningClient::refreshStatus() {
    if (m_mode != Mode::None) {
        finishRefresh(false, State::Idle, QString(), 0);
        return;
    }
    m_mode = Mode::Refresh;
    log(QStringLiteral("refreshStatus()"));
    m_terminalTimer->start(kRefreshTimeoutMs);

    m_controller = QLowEnergyController::createCentral(m_device, this);
    if (!m_controller) {
        finishRefresh(false, State::Idle, QString(), 0);
        return;
    }
    connect(m_controller, &QLowEnergyController::connected,
            this, &DecenzaProvisioningClient::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &DecenzaProvisioningClient::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &DecenzaProvisioningClient::onControllerError);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &DecenzaProvisioningClient::onServiceDiscoveryFinished);

    m_controller->connectToDevice();
}

void DecenzaProvisioningClient::forgetWifi() {
    if (m_mode != Mode::None) {
        finishForgetFailure(QStringLiteral("Operation already in progress"));
        return;
    }
    m_mode = Mode::Forget;

    log(QStringLiteral("forgetWifi()"));
    m_terminalTimer->start(kForgetTimeoutMs);

    m_controller = QLowEnergyController::createCentral(m_device, this);
    if (!m_controller) {
        finishForgetFailure(QStringLiteral("Failed to create BLE controller"));
        return;
    }
    connect(m_controller, &QLowEnergyController::connected,
            this, &DecenzaProvisioningClient::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &DecenzaProvisioningClient::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &DecenzaProvisioningClient::onControllerError);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &DecenzaProvisioningClient::onServiceDiscoveryFinished);

    m_controller->connectToDevice();
}

void DecenzaProvisioningClient::onControllerConnected() {
    log(QStringLiteral("BLE connected; discovering services"));
    m_controller->discoverServices();
}

void DecenzaProvisioningClient::onControllerDisconnected() {
    log(QStringLiteral("BLE disconnected"));
    if (m_finished) return;
    // Disconnect during an unfinished operation is fatal.
    if (m_mode == Mode::Forget) {
        finishForgetFailure(QStringLiteral("Disconnected before forget completed"));
    } else if (m_mode == Mode::Refresh) {
        finishRefresh(false, State::Idle, QString(), 0);
    } else {
        finishFailure(QStringLiteral("Disconnected before provisioning completed"));
    }
}

void DecenzaProvisioningClient::onControllerError(QLowEnergyController::Error err) {
    if (m_finished) return;
    const QString reason = QStringLiteral("BLE error: %1").arg(static_cast<int>(err));
    if (m_mode == Mode::Forget) finishForgetFailure(reason);
    else if (m_mode == Mode::Refresh) finishRefresh(false, State::Idle, QString(), 0);
    else finishFailure(reason);
}

void DecenzaProvisioningClient::onServiceDiscoveryFinished() {
    if (!m_controller->services().contains(kServiceUuid)) {
        const QString reason = QStringLiteral("Provisioning service not present on device");
        if (m_mode == Mode::Forget) finishForgetFailure(reason);
        else if (m_mode == Mode::Refresh) finishRefresh(false, State::Idle, QString(), 0);
        else finishFailure(reason);
        return;
    }
    m_service = m_controller->createServiceObject(kServiceUuid, this);
    if (!m_service) {
        const QString reason = QStringLiteral("Failed to create provisioning service object");
        if (m_mode == Mode::Forget) finishForgetFailure(reason);
        else if (m_mode == Mode::Refresh) finishRefresh(false, State::Idle, QString(), 0);
        else finishFailure(reason);
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged,
            this, &DecenzaProvisioningClient::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &DecenzaProvisioningClient::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::characteristicWritten,
            this, &DecenzaProvisioningClient::onCharacteristicWritten);
    m_service->discoverDetails(QLowEnergyService::SkipValueDiscovery);
}

void DecenzaProvisioningClient::onServiceStateChanged(
    QLowEnergyService::ServiceState state) {
    if (state != QLowEnergyService::RemoteServiceDiscovered) return;
    log(QStringLiteral("Provisioning service discovered"));

    if (m_mode == Mode::Forget) {
        // Forget path: write 0x02 to fee3 and report success once the write
        // is acknowledged. No STATUS subscription needed.
        const QLowEnergyCharacteristic ctrl = m_service->characteristic(kControlCharUuid);
        if (!ctrl.isValid()) {
            finishForgetFailure(QStringLiteral("Control characteristic not found"));
            return;
        }
        m_step = WriteStep::Control;
        QByteArray cmd;
        cmd.append(static_cast<char>(kCmdForget));
        m_service->writeCharacteristic(ctrl, cmd);
        return;
    }

    if (m_mode == Mode::Refresh) {
        // Refresh path: just read fee4 once and report the parsed result.
        const QLowEnergyCharacteristic status = m_service->characteristic(kStatusCharUuid);
        if (!status.isValid()) {
            finishRefresh(false, State::Idle, QString(), 0);
            return;
        }
        // The read result lands on QLowEnergyService::characteristicRead,
        // which is wired below in onServiceStateChanged via a dedicated
        // single-shot connection.
        connect(m_service, &QLowEnergyService::characteristicRead, this,
            [this](const QLowEnergyCharacteristic& c, const QByteArray& value) {
                if (c.uuid() != kStatusCharUuid) return;
                const StatusReading r = parseStatus(value);
                finishRefresh(r.state == State::Connected, r.state, r.ip, r.err);
            });
        m_service->readCharacteristic(status);
        return;
    }

    // Provisioning path: subscribe to STATUS first so we don't miss the
    // transition that happens immediately after writing the connect byte.
    const QLowEnergyCharacteristic status = m_service->characteristic(kStatusCharUuid);
    if (!status.isValid()) {
        finishFailure(QStringLiteral("STATUS characteristic not found"));
        return;
    }
    const QLowEnergyDescriptor cccd = status.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (cccd.isValid()) {
        m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    } else {
        log(QStringLiteral("STATUS CCCD not found — proceeding without explicit subscribe"));
    }

    writeProvisioningSequence();
}

void DecenzaProvisioningClient::writeProvisioningSequence() {
    const QLowEnergyCharacteristic ssidChar = m_service->characteristic(kSsidCharUuid);
    if (!ssidChar.isValid()) {
        finishFailure(QStringLiteral("SSID characteristic not found"));
        return;
    }
    m_step = WriteStep::Ssid;
    m_service->writeCharacteristic(ssidChar, m_ssid.toUtf8());
}

void DecenzaProvisioningClient::onCharacteristicWritten(
    const QLowEnergyCharacteristic& c, const QByteArray& /*value*/) {
    if (m_finished) return;

    if (m_mode == Mode::Forget) {
        if (c.uuid() == kControlCharUuid && m_step == WriteStep::Control) {
            finishForgetSuccess();
        }
        return;
    }

    // Provisioning path: chain SSID → passphrase → control.
    if (c.uuid() == kSsidCharUuid && m_step == WriteStep::Ssid) {
        const QLowEnergyCharacteristic passChar = m_service->characteristic(kPassCharUuid);
        if (!passChar.isValid()) {
            finishFailure(QStringLiteral("Passphrase characteristic not found"));
            return;
        }
        m_step = WriteStep::Pass;
        m_service->writeCharacteristic(passChar, m_passphrase.toUtf8());
        return;
    }
    if (c.uuid() == kPassCharUuid && m_step == WriteStep::Pass) {
        const QLowEnergyCharacteristic ctrl = m_service->characteristic(kControlCharUuid);
        if (!ctrl.isValid()) {
            finishFailure(QStringLiteral("Control characteristic not found"));
            return;
        }
        m_step = WriteStep::Control;
        QByteArray cmd;
        cmd.append(static_cast<char>(kCmdConnect));
        m_service->writeCharacteristic(ctrl, cmd);
        return;
    }
    if (c.uuid() == kControlCharUuid && m_step == WriteStep::Control) {
        log(QStringLiteral("Connect command sent; awaiting STATUS"));
        // Wait for STATUS notification — terminal signals come from
        // onCharacteristicChanged().
    }
}

void DecenzaProvisioningClient::onCharacteristicChanged(
    const QLowEnergyCharacteristic& c, const QByteArray& value) {
    if (m_finished) return;
    if (c.uuid() != kStatusCharUuid) return;

    const StatusReading r = parseStatus(value);
    log(QStringLiteral("STATUS state=%1 ip=%2 err=%3")
            .arg(static_cast<int>(r.state))
            .arg(r.ip.isEmpty() ? QStringLiteral("-") : r.ip)
            .arg(r.err));

    emit statusUpdate(static_cast<int>(r.state), r.ip, r.err);

    if (r.state == State::Connected) {
        finishSuccess(r.ip);
    } else if (r.state == State::Failed) {
        finishFailure(r.err == 0
            ? QStringLiteral("Wi-Fi connection failed")
            : QStringLiteral("Wi-Fi connection failed (err=%1)").arg(r.err));
    }
    // Idle / Connecting → keep waiting; the watchdog will eventually fire if
    // the firmware never reports a terminal state.
}

void DecenzaProvisioningClient::onTerminalTimeout() {
    if (m_finished) return;
    if (m_mode == Mode::Forget) {
        finishForgetFailure(QStringLiteral("Forget timed out"));
    } else if (m_mode == Mode::Refresh) {
        finishRefresh(false, State::Idle, QString(), 0);
    } else {
        finishFailure(QStringLiteral("Provisioning timed out"));
    }
}

DecenzaProvisioningClient::StatusReading DecenzaProvisioningClient::parseStatus(
    const QByteArray& data) {
    StatusReading r{State::Idle, 0, QString(), 0};
    if (data.size() < 7) return r;
    const auto* d = reinterpret_cast<const quint8*>(data.constData());
    r.state = static_cast<State>(d[0]);
    r.rssi = static_cast<qint8>(d[1]);
    if (r.state == State::Connected) {
        r.ip = QStringLiteral("%1.%2.%3.%4")
                   .arg(d[2]).arg(d[3]).arg(d[4]).arg(d[5]);
    }
    r.err = d[6];
    return r;
}

void DecenzaProvisioningClient::readWifiStatusOnce(
    QLowEnergyService* service,
    std::function<void(bool, State, QString, quint8)> callback) {
    if (!service) {
        callback(false, State::Idle, QString(), 0);
        return;
    }
    const QLowEnergyCharacteristic status = service->characteristic(kStatusCharUuid);
    if (!status.isValid()) {
        callback(false, State::Idle, QString(), 0);
        return;
    }
    // The read result arrives via QLowEnergyService::characteristicRead. Wire
    // a single-shot connection so we don't pollute the caller's signal flow.
    auto* conn = new QMetaObject::Connection;
    *conn = QObject::connect(service, &QLowEnergyService::characteristicRead, service,
        [conn, callback](const QLowEnergyCharacteristic& c, const QByteArray& value) {
            if (c.uuid() != kStatusCharUuid) return;
            QObject::disconnect(*conn);
            delete conn;
            const StatusReading r = parseStatus(value);
            callback(r.state == State::Connected, r.state, r.ip, r.err);
        });
    service->readCharacteristic(status);
}

void DecenzaProvisioningClient::finishSuccess(const QString& ip) {
    if (m_finished) return;
    m_finished = true;
    m_terminalTimer->stop();
    log(QStringLiteral("Provisioning succeeded: %1").arg(ip));
    emit provisioningCompleted(ip);
    deleteLater();
}

void DecenzaProvisioningClient::finishFailure(const QString& reason) {
    if (m_finished) return;
    m_finished = true;
    m_terminalTimer->stop();
    log(QStringLiteral("Provisioning failed: %1").arg(reason));
    emit provisioningFailed(reason);
    deleteLater();
}

void DecenzaProvisioningClient::finishForgetSuccess() {
    if (m_finished) return;
    m_finished = true;
    m_terminalTimer->stop();
    log(QStringLiteral("Forget succeeded"));
    emit forgetCompleted();
    deleteLater();
}

void DecenzaProvisioningClient::finishForgetFailure(const QString& reason) {
    if (m_finished) return;
    m_finished = true;
    m_terminalTimer->stop();
    log(QStringLiteral("Forget failed: %1").arg(reason));
    emit forgetFailed(reason);
    deleteLater();
}

void DecenzaProvisioningClient::finishRefresh(bool ok, State state,
                                              const QString& ip, quint8 err) {
    if (m_finished) return;
    m_finished = true;
    m_terminalTimer->stop();
    log(QStringLiteral("Refresh %1 (state=%2 ip=%3 err=%4)")
            .arg(ok ? "ok" : "failed")
            .arg(static_cast<int>(state))
            .arg(ip.isEmpty() ? QStringLiteral("-") : ip)
            .arg(err));
    emit statusRefreshed(ok, static_cast<int>(state), ip, err);
    deleteLater();
}
