#include <QtTest>
#include <QSignalSpy>
#include <QLowEnergyCharacteristic>
#include "ai/portaltailanalyzer.h"
#include "ai/shotsummarizer.h"
#include "ble/belkaportaldevice.h"
#include "ble/belkaportaldiscovery.h"
#include "ble/de1device.h"
#include "controllers/portalcontroller.h"
#include "controllers/shottimingcontroller.h"
#include "core/dbutils.h"
#include "core/settings.h"
#include "core/settings_hardware.h"
#include "core/settingsserializer.h"
#include "history/shothistorystorage.h"
#include "history/shotprojection.h"
#include "machine/machinestate.h"
#include "mcp/mcptools_shots_helpers.h"
#include "models/shotdatamodel.h"
#include "network/visualizeruploader.h"
#include <QScopeGuard>
#include <QJSEngine>
#include <QDirIterator>
#include <QFile>
#include <limits>

class PortalDiscoveryReplay : public QObject {
    Q_OBJECT
public:
    bool scanning = false;
    bool isScanning() const { return scanning; }
    void scanForDevices() {
        scanning = true;
        emit scanningChanged();
        emit scanStarted();
    }
signals:
    void scanningChanged();
    void scanStarted();
    void portalDiscovered(const QBluetoothDeviceInfo& device);
};

class PortalTransport : public ScaleBleTransport {
public:
    QStringList operations;
    QList<QByteArray> writes;
    bool linked = false;
    bool asynchronousDisconnect = false;
    bool disconnectPending = false;
    bool managed = true;
    void setConnectionPriorityManaged(bool value) override { managed = value; }
    void connectToDevice(const QString&, const QString&) override {
        linked = true; operations << "connect"; emit connected();
    }
    void disconnectFromDevice() override {
        disconnectPending = disconnectPending || (asynchronousDisconnect && linked);
        linked = false; operations << "disconnect";
        // Qt tears down synchronously without emitting disconnected().
    }
    bool isDisconnecting() const override { return disconnectPending; }
    int forgets = 0;
    void forgetTarget() override { ++forgets; }
    void completeDisconnect() { disconnectPending = false; emit disconnected(); }
    void discoverServices() override { operations << "services"; }
    void discoverCharacteristics(const QBluetoothUuid&) override { operations << "characteristics"; }
    void enableNotifications(const QBluetoothUuid&, const QBluetoothUuid& chr) override {
        operations << "subscribe"; emit notificationsIssued(chr); emit notificationsEnabled(chr);
    }
    void readCharacteristic(const QBluetoothUuid&, const QBluetoothUuid&) override { operations << "read"; }
    void writeCharacteristic(const QBluetoothUuid& svc, const QBluetoothUuid& chr, const QByteArray& bytes, WriteType type) override {
        QCOMPARE(svc, QBluetoothUuid(quint16(0x7400)));
        QCOMPARE(chr, QBluetoothUuid(quint16(0x7420)));
        QCOMPARE(type, WriteType::WithResponse);
        writes.append(bytes);
        operations << "write";
    }
    bool isConnected() const override { return linked; }

    void announceService(int properties = QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify) {
        const QBluetoothUuid service(quint16(0x7400)), value(quint16(0x7410));
        emit serviceDiscovered(service);
        emit servicesDiscoveryFinished();
        if (properties) emit characteristicDiscovered(service, value, properties);
        emit characteristicsDiscoveryFinished(service);
    }
    void notify(const QByteArray& bytes) { emit characteristicChanged(QBluetoothUuid(quint16(0x7410)), bytes); }
};

class PortalLogCapture {
public:
    QStringList lines;
    PortalLogCapture() { sink = &lines; previous = qInstallMessageHandler(handler); }
    ~PortalLogCapture() { qInstallMessageHandler(previous); sink = nullptr; }
private:
    inline static QStringList* sink = nullptr;
    inline static QtMessageHandler previous = nullptr;
    static void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
        if (message.startsWith("[PORTAL]")) sink->append(message);
        if (previous) previous(type, context, message);
    }
};

class tst_BelkaPortal : public QObject {
    Q_OBJECT
    const QBluetoothDeviceInfo device{QBluetoothAddress("11:22:33:44:55:66"), "PORTAL", 0};
    const QByteArray sample = QByteArray::fromHex("0000c03f0000c8410000ae42c8");
    void start(BelkaPortalDevice& portal, PortalTransport* transport) {
        portal.observeDevice(device);
        portal.connectDevice("11:22:33:44:55:66");
        transport->announceService();
    }

private slots:
    void init() { QTest::failOnWarning(); }
    void historyCursorUsesRecordedSegments() {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/qml/components/GraphUtils.js"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString source = QString::fromUtf8(file.readAll());
        source.remove(QStringLiteral(".pragma library"));
        QJSEngine engine;
        const auto load = engine.evaluate(source);
        QVERIFY2(!load.isError(), qPrintable(load.toString()));
        const auto lookup = engine.globalObject().property("portalReadingAtTime");
        QVERIFY(lookup.isCallable());
        const auto samples = engine.evaluate(QStringLiteral(R"([
            {time:1, ecRaw:0, temperatureC:20, breakBefore:true},
            {time:2, ecRaw:2, temperatureC:40, breakBefore:false},
            {time:3, ecRaw:4, temperatureC:60, breakBefore:true},
            {time:4, ecRaw:6, temperatureC:80, breakBefore:false}
        ])"));
        const auto at = [&](double time) { return lookup.call({samples, QJSValue(time)}); };
        QCOMPARE(at(1).property("ecRaw").toNumber(), 0.0);
        QCOMPARE(at(1.5).property("ecRaw").toNumber(), 1.0);
        QCOMPARE(at(1.5).property("temperatureC").toNumber(), 30.0);
        QVERIFY(at(0.99).isNull());
        QVERIFY(at(4.01).isNull());
        QVERIFY(at(2.01).isNull()); // even a short recorded interruption stays empty
        QVERIFY(at(2.99).isNull());
        QCOMPARE(at(3).property("ecRaw").toNumber(), 4.0);
        QCOMPARE(at(3.5).property("temperatureC").toNumber(), 70.0);
        QCOMPARE(at(4).property("ecRaw").toNumber(), 6.0);
        QVERIFY(lookup.call({engine.newArray(), QJSValue(1)}).isNull());
        const auto single = engine.evaluate(QStringLiteral("[{time:2,ecRaw:0.073,temperatureC:23}]"));
        QCOMPARE(lookup.call({single, QJSValue(2)}).property("ecRaw").toNumber(), 0.073);
        QVERIFY(lookup.call({single, QJSValue(2.01)}).isNull());
    }
    void stateLoggingCollapsesWithinEachShotAndReportsEveryLinkLoss() {
        PortalLogCapture logs;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        transport->notify(sample);
        portal.setExtractionActive(true);
        portal.expireReading(PortalSamples::StaleAfterMs + 1);
        transport->notify(sample);
        const auto initialLines = logs.lines;
        for (int i = 0; i < 100; ++i) {
            portal.expireReading(PortalSamples::StaleAfterMs + 1);
            transport->notify(sample);
        }
        QCOMPARE(logs.lines, initialLines);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[PORTAL\\]\\[BLE\\] Unknown PORTAL measurement format; raw packet retained: length=12, hex=.*"));
        QByteArray malformed(12, '\0');
        transport->notify(malformed);
        const auto afterMalformed = logs.lines;
        for (int i = 0; i < 100; ++i) {
            malformed[11] = static_cast<char>(i);
            transport->notify(malformed);
        }
        QCOMPARE(logs.lines, afterMalformed); // raw diagnostics must not defeat collapsing
        transport->notify(sample);
        portal.setExtractionActive(false);
        QVERIFY(logs.lines.size() > initialLines.size());
        for (int i = 0; i < 2; ++i) {
            QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL disconnected; reconnect when the machine is idle");
            transport->linked = false;
            transport->completeDisconnect();
            portal.reconnect();
            QTRY_VERIFY(portal.active());
            transport->announceService();
            transport->notify(sample);
        }
        QCOMPARE(logs.lines.filter("PORTAL disconnected;").size(), 2);
        QVERIFY(logs.lines.filter("Receiving measurements").size() >= 3);
    }

    void transportIsCreatedOnlyAfterSelection() {
        int creations = 0;
        BelkaPortalDevice portal([&] { ++creations; return new PortalTransport; });
        portal.restoreSavedDevice("11:22:33:44:55:66", "PORTAL");
        portal.setMachineBusy(true);
        portal.observeDevice(device);
        QCOMPARE(creations, 0);
        portal.setMachineBusy(false);
        QTRY_COMPARE(creations, 1);
    }

    void classifierRecognizesServiceOrName() {
        QVERIFY(BelkaPortalDevice::isPortal(device));
        QBluetoothDeviceInfo serviceOnly(QBluetoothAddress("11:22:33:44:55:66"), "", 0);
        serviceOnly.setServiceUuids({QBluetoothUuid(quint16(0x7400))});
        QVERIFY(BelkaPortalDevice::isPortal(serviceOnly));
        const QBluetoothDeviceInfo scale(QBluetoothAddress("22:22:33:44:55:66"), "Bookoo", 0);
        QVERIFY(!BelkaPortalDevice::isPortal(scale));
    }

    void firmwareUpdateDisconnectsAndRestoresSavedPortal()
    {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        const QString address = portal.savedAddress();
        QVERIFY(!address.isEmpty());
        portal.setFirmwareUpdateActive(true);
        QCOMPARE(portal.state(), "disconnected");
        QVERIFY(!transport->linked);
        QCOMPARE(portal.savedAddress(), address);
        portal.connectDevice(address);
        QVERIFY(!portal.reconnect());
        portal.setMachineBusy(false);
        portal.observeDevice(device);
        QTest::qWait(20);
        QCOMPARE(transport->operations.count("connect"), 1);
        portal.setFirmwareUpdateActive(false);
        QTRY_COMPARE(transport->operations.count("connect"), 2);
    }

    void synchronousDisconnectCanReconnectForgetAndRecoverFromError() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        portal.disconnectDevice();
        QCOMPARE(portal.state(), "disconnected");
        portal.reconnect();
        QTRY_COMPARE(transport->operations.count("connect"), 2);
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] Connection failed");
        emit transport->error("Connection failed");
        QCOMPARE(portal.state(), "error");
        portal.reconnect();
        QTRY_COMPARE(transport->operations.count("connect"), 3);
        transport->announceService();
        portal.forgetDevice();
        QCOMPARE(portal.state(), "disconnected");
        QVERIFY(portal.savedAddress().isEmpty());
        portal.connectDevice("11:22:33:44:55:66");
        QCOMPARE(transport->operations.count("connect"), 4);
    }

    void subscriptionFailureAndFirstNotificationDeadlineAreTerminal() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL measurement subscription failed");
        emit transport->gattOperationFailed(QBluetoothUuid(quint16(0x7410)));
        QCOMPARE(portal.state(), "error");
        QVERIFY(!portal.active());
        portal.connectDevice("11:22:33:44:55:66");
        transport->announceService();
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL sent no valid measurement after notification setup");
        QTRY_COMPARE_WITH_TIMEOUT(portal.state(), "error", 7000);
        QVERIFY(!portal.hasReading());
    }

    void healthTimerDetectsStaleDataAndRecovers() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        transport->notify(sample);
        QTRY_COMPARE_WITH_TIMEOUT(portal.state(), "stale", 7000);
        QVERIFY(!portal.hasReading());
        transport->notify(sample);
        QVERIFY(portal.hasReading());
    }

    void everyMachinePhaseAppliesConnectionPolicy() {
        Settings settings;
        DE1Device de1;
        MachineState machine(&de1);
        ShotTimingController timing(&de1);
        ShotDataModel model;
        BelkaPortalDevice portal(new PortalTransport);
        PortalController controller(&portal, settings.hardware(), &machine, &timing, &model);
        using Phase = MachineState::Phase;
        const QList<Phase> available{Phase::Disconnected, Phase::Sleep, Phase::Idle, Phase::Heating, Phase::Ready, Phase::Refill};
        const QList<Phase> busy{Phase::EspressoPreheating, Phase::Preinfusion, Phase::Pouring,
            Phase::Ending, Phase::Steaming, Phase::HotWater, Phase::Flushing,
            Phase::Descaling, Phase::Cleaning, Phase::Transport};
        for (const auto phase : busy) {
            machine.m_phase = phase;
            emit machine.phaseChanged();
            QVERIFY(portal.machineBusy());
        }
        for (const auto phase : available) {
            machine.m_phase = phase;
            emit machine.phaseChanged();
            QVERIFY(!portal.machineBusy());
        }
    }

    void refillDoesNotStrandPortalAfterStartup_data() {
        QTest::addColumn<bool>("freshScan");
        QTest::newRow("cached-device") << false;
        QTest::newRow("fresh-discovery") << true;
    }

    void refillDoesNotStrandPortalAfterStartup() {
        QFETCH(bool, freshScan);
        Settings settings;
        auto* hardware = settings.hardware();
        const auto oldAddress = hardware->portalAddress(), oldName = hardware->portalName();
        const auto restore = qScopeGuard([&] { hardware->setPortalDevice(oldAddress, oldName); });
        hardware->setPortalDevice({}, {});
        DE1Device de1;
        MachineState machine(&de1);
        ShotTimingController timing(&de1);
        ShotDataModel model;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        PortalController controller(&portal, hardware, &machine, &timing, &model);
        PortalDiscoveryReplay discovery;
        connectPortalDiscovery(&discovery, &portal);
        QSignalSpy scan(&portal, &BelkaPortalDevice::scanRequested);
        start(portal, transport);
        transport->notify(sample);
        machine.m_phase = MachineState::Phase::Refill;
        emit machine.phaseChanged();
        QVERIFY(portal.hasReading());
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL disconnected; reconnect when the machine is idle");
        transport->linked = false;
        transport->completeDisconnect();
        if (freshScan) portal.clearDevices();
        portal.reconnect(); // Same entry point as the status bar and Settings.
        QCOMPARE(scan.count(), freshScan ? 1 : 0);
        if (freshScan) emit discovery.portalDiscovered(device);
        QTRY_COMPARE(transport->operations.count("connect"), 2);
        transport->announceService();
        transport->notify(sample);
        QVERIFY(portal.hasReading());
        QCOMPARE(portal.ecRaw(), 1.5);
        portal.disconnectDevice();
        QCOMPARE(portal.state(), "disconnected");
        portal.connectDevice("11:22:33:44:55:66"); // Settings scan-result selection also works.
        QCOMPARE(transport->operations.count("connect"), 3);
        machine.m_phase = MachineState::Phase::Pouring;
        emit machine.phaseChanged();
        portal.reconnect();
        QCoreApplication::sendPostedEvents();
        QCOMPARE(transport->operations.count("connect"), 3);
    }

    void liveBackupRestoreReplacesConnectionAndDisplayPreference() {
        Settings settings;
        auto* hardware = settings.hardware();
        const auto oldAddress = hardware->portalAddress(), oldName = hardware->portalName();
        const bool oldSync = hardware->portalSyncDisplay();
        const auto restore = qScopeGuard([&] {
            hardware->setPortalDevice(oldAddress, oldName);
            hardware->setPortalSyncDisplay(oldSync);
        });
        hardware->setPortalDevice({}, {});
        hardware->setPortalSyncDisplay(true);
        DE1Device de1;
        MachineState machine(&de1);
        ShotTimingController timing(&de1);
        ShotDataModel model;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        PortalController controller(&portal, hardware, &machine, &timing, &model);
        QSignalSpy scan(&portal, &BelkaPortalDevice::scanRequested);
        start(portal, transport);
        transport->notify(sample);
        QCOMPARE(hardware->portalAddress(), portal.savedAddress());
        transport->asynchronousDisconnect = true;
        const QBluetoothDeviceInfo replacement(QBluetoothAddress("22:33:44:55:66:77"), "PORTAL replacement", 0);
        portal.observeDevice(replacement);
        QJsonObject backup{{"portal", QJsonObject{{"address", "22:33:44:55:66:77"},
            {"name", "PORTAL replacement"}, {"syncDisplay", false}}}};
        QVERIFY(SettingsSerializer::importFromJson(&settings, backup));
        QVERIFY(!portal.syncDisplay());
        QVERIFY(!portal.hasReading());
        QCOMPARE(portal.savedAddress(), QString("22:33:44:55:66:77"));
        QCoreApplication::sendPostedEvents();
        QCOMPARE(transport->operations.count("connect"), 1); // old disconnect still outstanding
        transport->notify(sample); // late packets cannot restore the old selection
        QVERIFY(!portal.hasReading());
        transport->completeDisconnect();
        QTRY_COMPARE(transport->operations.count("connect"), 2);
        transport->announceService();
        QCOMPARE(hardware->portalAddress(), QString("22:33:44:55:66:77"));
        QCOMPARE(hardware->portalName(), QString("PORTAL replacement"));
        QCOMPARE(scan.count(), 0); // restore/reconnect piggybacks existing discovery
        portal.setSyncDisplay(true);
        QVERIFY(hardware->portalSyncDisplay());
        QVERIFY(SettingsSerializer::importFromJson(&settings, backup));
        QVERIFY(!portal.syncDisplay());
    }

    void reconnectWaitsForDisconnectCompletion() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        transport->asynchronousDisconnect = true;
        portal.disconnectDevice();
        portal.reconnect();
        QCoreApplication::sendPostedEvents();
        QCOMPARE(transport->operations.count("connect"), 1);
        QCOMPARE(portal.state(), QString("disconnecting"));
        transport->completeDisconnect();
        QTRY_COMPARE(transport->operations.count("connect"), 2);
        transport->announceService();
        transport->notify(sample);
        QVERIFY(portal.hasReading());
    }

    void failedDisplayWriteDoesNotStopMeasurements() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        emit transport->characteristicDiscovered(QBluetoothUuid(quint16(0x7400)),
            QBluetoothUuid(quint16(0x7420)), QLowEnergyCharacteristic::Write);
        transport->notify(sample);
        portal.setExtractionActive(true);
        portal.setMachineBusy(true);
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL display command 40 00 01 2d was not acknowledged");
        emit transport->gattOperationFailed(QBluetoothUuid(quint16(0x7420)));
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] Service error: CharacteristicWriteError");
        emit transport->error("Service error: CharacteristicWriteError");
        QCOMPARE(portal.displayCommandStatus(), QString("failed"));
        transport->notify(sample);
        QVERIFY(portal.hasReading());
        QVERIFY(portal.active());
        portal.setExtractionActive(false);
        QCOMPARE(transport->writes.last(), QByteArray::fromHex("4000012e"));
        emit transport->characteristicWritten(QBluetoothUuid(quint16(0x7420)));
        QCOMPARE(portal.displayCommandStatus(), "acknowledged");
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] Read failed");
        emit transport->error("Read failed");
        QVERIFY(portal.hasReading());
    }

    void lastDisplayCompletionWinsAfterEarlierFailure() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        emit transport->characteristicDiscovered(QBluetoothUuid(quint16(0x7400)),
            QBluetoothUuid(quint16(0x7420)), QLowEnergyCharacteristic::Write);
        transport->notify(sample);
        portal.setExtractionActive(true);
        portal.setExtractionActive(false);
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL display command 40 00 01 2d was not acknowledged");
        emit transport->gattOperationFailed(QBluetoothUuid(quint16(0x7420)));
        QCOMPARE(portal.displayCommandStatus(), "requested");
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] Service error: CharacteristicWriteError");
        emit transport->error("Service error: CharacteristicWriteError");
        emit transport->characteristicWritten(QBluetoothUuid(quint16(0x7420)));
        QCOMPARE(portal.displayCommandStatus(), "acknowledged");
        portal.disconnectDevice();
        QVERIFY(portal.displayCommandStatus().isEmpty());
    }

    void captureFollowsRealExtractionClockAndFinalization() {
        Settings settings;
        auto* hardware = settings.hardware();
        const auto address = hardware->portalAddress(), name = hardware->portalName();
        const auto restore = qScopeGuard([&] { hardware->setPortalDevice(address, name); });
        hardware->setPortalDevice({}, {});
        DE1Device de1;
        MachineState machine(&de1);
        ShotTimingController timing(&de1);
        ShotDataModel model;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        PortalController controller(&portal, hardware, &machine, &timing, &model);
        start(portal, transport);
        portal.setSyncDisplay(true);
        emit transport->characteristicDiscovered(QBluetoothUuid(quint16(0x7400)),
            QBluetoothUuid(quint16(0x7420)), QLowEnergyCharacteristic::Write);
        transport->notify(sample);
        QVERIFY(model.portalSamples().isEmpty());
        timing.startShot();
        transport->notify(sample); // espresso preheat has no extraction origin
        QVERIFY(model.portalSamples().isEmpty());
        ShotSample shot;
        timing.onShotSample(shot, 9.0, 2.0, 93.0, 2, false);
        transport->notify(sample);
        QCOMPARE(model.portalSampleCount(), 1);
        QVERIFY(model.portalSamples().first().time < 1.0);
        portal.expireReading(PortalSamples::StaleAfterMs + 1);
        transport->notify(sample);
        QCOMPARE(model.portalSampleCount(), 2);
        QVERIFY(model.portalSamples().last().breakBefore);
        QCOMPARE(transport->writes, QList<QByteArray>{QByteArray::fromHex("4000012d")});
        timing.endShot();
        QCOMPARE(transport->writes.last(), QByteArray::fromHex("4000012e"));
        QCOMPARE(transport->writes.size(), 2);
        portal.setGraphView(true);
        QCOMPARE(transport->writes.size(), 3); // finalization released the manual-control gate
        transport->notify(sample);
        QCOMPARE(model.portalSampleCount(), 2);
        model.clear(); // the normal shot-start wiring clears the previous recording
        timing.startShot();
        timing.onShotSample(shot, 9.0, 2.0, 93.0, 2, false);
        transport->notify(sample);
        QCOMPARE(model.portalSampleCount(), 1);
        timing.onSawTriggered(35.0, 1.0, 36.0);
        timing.endShot();
        QVERIFY(timing.isSawSettling());
        transport->notify(sample);
        QCOMPARE(model.portalSampleCount(), 2); // capture remains open through real SAW settling
        emit machine.phaseChanged(); // disconnected machine closes capture even before finalization
        transport->notify(sample);
        QCOMPARE(model.portalSampleCount(), 2);
    }

    void sharedScanKeepsPortalSelectableAfterWifiAndUsbFinish() {
        PortalDiscoveryReplay discovery;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        connectPortalDiscovery(&discovery, &portal);
        QSignalSpy reconnectScans(&portal, &BelkaPortalDevice::scanRequested);
        portal.setMachineBusy(true);
        QVERIFY(!portal.reconnect());
        QCOMPARE(reconnectScans.count(), 0);
        portal.setMachineBusy(false);
        portal.setExtractionActive(true);
        QVERIFY(!portal.reconnect());
        QCOMPARE(reconnectScans.count(), 0);
        portal.setExtractionActive(false);
        QVERIFY(portal.reconnect()); // no saved device: request the common device scan
        QCOMPARE(reconnectScans.count(), 1);
        QVERIFY(discovery.isScanning());
        emit discovery.portalDiscovered(device);
        QCOMPARE(portal.devices().size(), 1);
        QSignalSpy changes(&portal, &BelkaPortalDevice::devicesChanged);

        // Reproduce the owner's p2 log: PORTAL found, WiFi probe completes,
        // then other scan legs report progress while BLE remains active.
        emit discovery.scanningChanged();
        emit discovery.scanningChanged();
        QCOMPARE(portal.devices().size(), 1);
        QCOMPARE(changes.count(), 0);
        discovery.scanning = false;
        emit discovery.scanningChanged();
        QCOMPARE(portal.devices().size(), 1);
        portal.connectDevice("11:22:33:44:55:66");
        QCOMPARE(transport->operations.count("connect"), 1);
        QVERIFY(!portal.reconnect()); // active links reject reconnect without dispatching a scan
        QVERIFY(portal.savedAddress().isEmpty()); // discovery alone does not expose the panel
        transport->announceService();
        QCOMPARE(portal.savedAddress(), "11:22:33:44:55:66");

        portal.disconnectDevice();
        discovery.scanForDevices();
        QVERIFY(portal.devices().isEmpty());
        QCOMPARE(changes.count(), 1);
        emit discovery.portalDiscovered(device);
        emit discovery.portalDiscovered(device);
        QCOMPARE(portal.devices().size(), 1); // repeated advertisements deduplicate
    }
    void displaySyncWritesOnceAndAcknowledgesOnlyAllWrites() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        emit transport->characteristicDiscovered(QBluetoothUuid(quint16(0x7400)),
            QBluetoothUuid(quint16(0x7420)), QLowEnergyCharacteristic::Write);
        transport->notify(sample);
        portal.setMachineBusy(true);
        portal.setGraphView(true); // manual controls cannot write during a shot
        QVERIFY(transport->writes.isEmpty());
        portal.setExtractionActive(true);
        portal.setExtractionActive(true);
        QCOMPARE(transport->writes, QList<QByteArray>{QByteArray::fromHex("4000012d")});
        portal.setExtractionActive(false);
        portal.setExtractionActive(false);
        QCOMPARE(transport->writes, QList<QByteArray>({QByteArray::fromHex("4000012d"), QByteArray::fromHex("4000012e")}));
        QCOMPARE(portal.displayCommandStatus(), "requested");
        emit transport->characteristicWritten(QBluetoothUuid(quint16(0x7420)));
        QCOMPARE(portal.displayCommandStatus(), "requested");
        emit transport->characteristicWritten(QBluetoothUuid(quint16(0x7420)));
        QCOMPARE(portal.displayCommandStatus(), "acknowledged");
    }

    void unsupportedOrDisabledDisplayStillStreamsWithoutWrites() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        transport->notify(sample);
        QVERIFY(!portal.canControlDisplay());
        portal.setExtractionActive(true);
        portal.setExtractionActive(false);
        emit transport->characteristicDiscovered(QBluetoothUuid(quint16(0x7400)),
            QBluetoothUuid(quint16(0x7420)), QLowEnergyCharacteristic::Write);
        portal.setSyncDisplay(false);
        portal.setExtractionActive(true);
        transport->notify(sample);
        QVERIFY(portal.hasReading());
        portal.setExtractionActive(false);
        QVERIFY(transport->writes.isEmpty());
    }

    void savedDeviceReconnectIsBoundedAndManualDisconnectIsRespected() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        portal.restoreSavedDevice("11:22:33:44:55:66", "My PORTAL");
        portal.setMachineBusy(true);
        portal.observeDevice(device);
        QCoreApplication::sendPostedEvents();
        QVERIFY(transport->operations.isEmpty());
        portal.setMachineBusy(false);
        QTRY_COMPARE(transport->operations.count("connect"), 1);
        transport->announceService();
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL disconnected; reconnect when the machine is idle");
        transport->linked = false;
        transport->completeDisconnect();
        portal.observeDevice(device);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(transport->operations.count("connect"), 1);
        portal.reconnect();
        QTRY_COMPARE(transport->operations.count("connect"), 2);
        portal.disconnectDevice();
        portal.setMachineBusy(true);
        portal.setMachineBusy(false);
        portal.beginScan();
        portal.observeDevice(device);
        QCoreApplication::sendPostedEvents();
        QCOMPARE(transport->operations.count("connect"), 2);
        QCOMPARE(portal.savedAddress(), "11:22:33:44:55:66");
        portal.forgetDevice();
        QVERIFY(portal.savedAddress().isEmpty());
    }

    void subscriptionDoesNotClaimLiveData() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        QSignalSpy readings(&portal, &BelkaPortalDevice::measurementReceived);
        start(portal, transport);
        QVERIFY(!transport->managed);
        QCOMPARE(transport->operations, QStringList({"connect", "services", "characteristics", "subscribe"}));
        QCOMPARE(portal.state(), "waiting");
        QVERIFY(!portal.hasReading());
        emit transport->characteristicRead(QBluetoothUuid(quint16(0x7410)), sample);
        QVERIFY(!portal.hasReading());
        QCOMPARE(readings.count(), 0);
        transport->notify(sample);
        QCOMPARE(portal.state(), "streaming");
        QVERIFY(portal.hasReading());
        QCOMPARE(portal.ecRaw(), 1.5);
        QCOMPARE(portal.temperatureC(), 87.0);
        QCOMPARE(readings.count(), 1);
    }

    void missingCharacteristicIsNotConnected() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        portal.observeDevice(device);
        portal.connectDevice("11:22:33:44:55:66");
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL measurement 7410 does not support notifications");
        transport->announceService(QLowEnergyCharacteristic::Read);
        QCOMPARE(portal.state(), "error");
        QVERIFY(!portal.active());
        QVERIFY(!transport->operations.contains("subscribe"));
    }

    void invalidStaleAndLatePacketsCannotShowOldValues() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        QSignalSpy readings(&portal, &BelkaPortalDevice::measurementReceived);
        start(portal, transport);
        transport->notify(sample);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[PORTAL\\]\\[BLE\\] Unknown PORTAL measurement format; raw packet retained: length=12, hex=.*"));
        transport->notify(QByteArray(12, '\0'));
        QVERIFY(!portal.hasReading());
        QCOMPARE(readings.count(), 1);
        transport->notify(sample);
        QVERIFY(portal.hasReading());
        portal.expireReading(5001);
        QVERIFY(!portal.hasReading());
        QCOMPARE(portal.state(), "stale");
        transport->notify(sample);
        QVERIFY(portal.hasReading());
        QTest::ignoreMessage(QtWarningMsg, "[PORTAL][BLE] PORTAL disconnected; reconnect when the machine is idle");
        transport->linked = false;
        transport->completeDisconnect();
        QVERIFY(!portal.hasReading());
        transport->notify(sample);
        QVERIFY(!portal.hasReading());
        QCOMPARE(readings.count(), 3);
        portal.connectDevice("11:22:33:44:55:66");
        transport->announceService();
        QVERIFY(!portal.hasReading());
        QCOMPARE(portal.packetCount(), 0);
        transport->notify(sample);
        QVERIFY(portal.hasReading());
        QCOMPARE(readings.count(), 4);
    }

    void machineActivityBlocksNewConnectionsButKeepsStreaming() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        portal.observeDevice(device);
        portal.setMachineBusy(true);
        portal.connectDevice("11:22:33:44:55:66");
        QVERIFY(transport->operations.isEmpty());
        portal.setMachineBusy(false);
        start(portal, transport);
        transport->notify(sample);
        const auto before = transport->operations;
        portal.setMachineBusy(true);
        transport->notify(sample);
        QCOMPARE(transport->operations, before);
        QVERIFY(portal.hasReading());
        QVERIFY(!transport->operations.contains("write"));
    }

    void partialBackupLeavesSavedDeviceAlone() {
        Settings settings;
        auto* hardware = settings.hardware();
        const auto oldAddress = hardware->portalAddress(), oldName = hardware->portalName();
        const bool oldSync = hardware->portalSyncDisplay();
        const auto restore = qScopeGuard([&] {
            hardware->setPortalDevice(oldAddress, oldName);
            hardware->setPortalSyncDisplay(oldSync);
        });
        hardware->setPortalDevice("11:22:33:44:55:66", "PORTAL");
        hardware->setPortalSyncDisplay(true);
        QVERIFY(SettingsSerializer::importFromJson(&settings,
            QJsonObject{{"portal", QJsonObject{{"syncDisplay", false}}}}));
        QVERIFY(!hardware->portalSyncDisplay());
        QCOMPARE(hardware->portalAddress(), QString("11:22:33:44:55:66"));
        QCOMPARE(hardware->portalName(), QString("PORTAL"));
        QVERIFY(SettingsSerializer::importFromJson(&settings,
            QJsonObject{{"portal", QJsonObject{{"name", "Renamed"}}}}));
        QCOMPARE(hardware->portalAddress(), QString("11:22:33:44:55:66"));
        QCOMPARE(hardware->portalName(), QString("Renamed"));
    }

    void errorMessageNotifiesEvenWhenStateDoesNot() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        QCOMPARE(portal.state(), QString("waiting"));
        QSignalSpy errors(&portal, &BelkaPortalDevice::errorMessageChanged);
        QSignalSpy states(&portal, &BelkaPortalDevice::stateChanged);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[PORTAL\\]\\[BLE\\] Unknown PORTAL measurement format.*"));
        transport->notify(QByteArray(12, '\0'));
        QCOMPARE(portal.state(), QString("waiting")); // no state transition...
        QCOMPARE(states.count(), 0);
        QCOMPARE(errors.count(), 1);                  // ...but the message still notifies
        QVERIFY(!portal.errorMessage().isEmpty());
        transport->notify(sample);
        QCOMPARE(errors.count(), 2);
        QVERIFY(portal.errorMessage().isEmpty());
    }

    void malformedShapesCollapsePerLength() {
        PortalLogCapture logs;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[PORTAL\\]\\[BLE\\] .*length=7.*"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[PORTAL\\]\\[BLE\\] .*length=9.*"));
        for (int i = 0; i < 20; ++i) {
            transport->notify(QByteArray(7, '\0'));
            transport->notify(QByteArray(9, '\0')); // alternating shapes must not defeat collapsing
        }
        // The first-packet shape line also names a length, so match the malformed line itself.
        QCOMPARE(logs.lines.filter("retained: length=7").size(), 1);
        QCOMPARE(logs.lines.filter("retained: length=9").size(), 1);
    }

    void staleReadingStopsTheHealthTimerUntilDataReturns() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        start(portal, transport);
        transport->notify(sample);
        QVERIFY(portal.m_healthTimer.isActive());
        portal.expireReading(PortalSamples::StaleAfterMs + 1);
        QCOMPARE(portal.state(), QString("stale"));
        QVERIFY(!portal.m_healthTimer.isActive());
        transport->notify(sample);
        QVERIFY(portal.m_healthTimer.isActive());
    }

    void refusedActionsWhileBusyAreLogged() {
        PortalLogCapture logs;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        portal.restoreSavedDevice("11:22:33:44:55:66", "PORTAL");
        portal.setMachineBusy(true);
        portal.reconnect();
        portal.forgetDevice();
        QCOMPARE(logs.lines.filter("Reconnect ignored: machine busy").size(), 1);
        QCOMPARE(logs.lines.filter("Forget ignored: machine busy").size(), 1);
        QVERIFY(portal.owned()); // refusal must not have acted
        portal.setMachineBusy(false);
        portal.setExtractionActive(true);
        portal.disconnectDevice();
        QCOMPARE(logs.lines.filter("Disconnect ignored: shot in progress").size(), 1);
    }

    void ownedFollowsSavedDeviceAndOnlyForgetDropsTheTransportTarget() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        QSignalSpy owned(&portal, &BelkaPortalDevice::savedDeviceChanged);
        QVERIFY(!portal.owned());
        portal.restoreSavedDevice("11:22:33:44:55:66", "PORTAL");
        QVERIFY(portal.owned());
        QCOMPARE(owned.count(), 1);
        portal.disconnectDevice();
        QCOMPARE(transport->forgets, 0); // a routine disconnect keeps the reconnect target
        portal.forgetDevice();
        QCOMPARE(transport->forgets, 1);
        QVERIFY(!portal.owned());
    }

    void clearingAnEmptyDeviceListDoesNotNotify() {
        BelkaPortalDevice portal(new PortalTransport);
        QSignalSpy changed(&portal, &BelkaPortalDevice::devicesChanged);
        portal.beginScan();
        QCOMPARE(changed.count(), 0); // a non-owner's every scan must be invisible
        portal.observeDevice(device);
        QCOMPARE(changed.count(), 1);
        portal.beginScan();
        QCOMPARE(changed.count(), 2);
    }

    void sampleLoadReportsWhatItDropped() {
        const QVariantList values{
            QVariantMap{{"time", 0.0}, {"ecRaw", 1.0}, {"temperatureC", 90.0}, {"breakBefore", false}},
            QVariantMap{{"time", "bad"}, {"ecRaw", 1.0}, {"temperatureC", 90.0}},
            QVariantMap{{"time", 0.2}, {"ecRaw", 1.1}, {"temperatureC", 90.5}, {"breakBefore", false}}};
        int dropped = -1;
        const auto samples = PortalSamples::fromVariant(values, &dropped);
        QCOMPARE(dropped, 1);
        QCOMPARE(samples.size(), 2);
        QVERIFY(samples.at(1).breakBefore); // the dropped entry leaves a gap, not a bridge
    }

    void nonOwnerUiIsGatedOnOneDefinitionAndNeverOnAmbientDiscovery() {
        const auto source = [](const QString& relative) {
            QFile file(QStringLiteral(DECENZA_SOURCE_DIR "/") + relative);
            return file.open(QIODevice::ReadOnly | QIODevice::Text)
                ? QString::fromUtf8(file.readAll()) : QString();
        };
        // "Owns a PORTAL" has one definition; spelling it per site is how the gates drifted apart.
        QStringList drifted;
        QDirIterator it(QStringLiteral(DECENZA_SOURCE_DIR "/qml"), {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
        int scanned = 0;
        while (it.hasNext()) {
            const auto path = it.next();
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            ++scanned;
            if (QString::fromUtf8(file.readAll()).contains(QRegularExpression(R"(BelkaPortal\.savedAddress\.length)")))
                drifted << path;
        }
        QVERIFY2(scanned > 100, "the QML tree was not found; this scan would pass vacuously");
        QVERIFY2(drifted.isEmpty(), qPrintable("use BelkaPortal.owned in: " + drifted.join(", ")));

        // An unpaired PORTAL broadcasting nearby must not reshape the Connections tab, and the
        // panel must not even be constructed for someone who has none.
        const auto tab = source("qml/pages/settings/SettingsConnectionsTab.qml");
        QVERIFY(!tab.isEmpty());
        QVERIFY2(!tab.contains(QRegularExpression(R"(\|\|\s*BelkaPortal\.devices\.length)")),
                 "discovered PORTALs must only affect the list after this tab triggered a scan");
        QVERIFY2(tab.contains(QRegularExpression(
                     R"(connectionsTab\.portalScanTriggered\s*&&\s*connectionsTab\.portalState\.devices\.length\s*>\s*0)")),
                 "discovered PORTAL entries must only change the list after this tab starts scanning");
        QVERIFY(tab.contains(
            "active: connectionsTab.portalState.owned || connectionsTab.portalState.state !== \"disconnected\""));
        QVERIFY(tab.contains("connectionsTab.portalScanTriggered = true"));
        QVERIFY(tab.contains("sourceComponent: PortalDevicePanel"));

        // PORTAL curves are advanced-only everywhere: the legend entries carry the flag, and the
        // overlay reads the visibility settings once, behind the host chart's advanced mode.
        const auto series = source("qml/components/GraphSeries.qml");
        QCOMPARE(series.count(QRegularExpression(R"(advanced: true, portal: true)")), 2);
        const auto overlay = source("qml/components/PortalGraphOverlay.qml");
        QCOMPARE(overlay.count("Settings.graph.showPortal"), 2);
        QVERIFY(overlay.contains("advancedMode && Settings.graph.showPortalEc"));
        QVERIFY(overlay.contains("advancedMode && Settings.graph.showPortalTemperature"));
    }

    void machineStartCancelsPendingDiscovery() {
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        portal.observeDevice(device);
        portal.connectDevice("11:22:33:44:55:66");
        portal.setMachineBusy(true);
        transport->announceService();
        QVERIFY(!portal.active());
        QVERIFY(!transport->operations.contains("read"));
        QVERIFY(!transport->operations.contains("subscribe"));
    }

    void portalIntegrationRespectsScalePlateausAndPartialWindows() {
        QVector<PortalSample> portal;
        for (int i = 0; i <= 100; ++i)
            portal.append({i / 10.0, 20.0 - i / 10.0, 55.0, i == 0});
        PortalTailAnalyzer::Options opts;
        opts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
        const auto plateau = PortalTailAnalyzer::analyze(portal, {{0, 0}, {5, 0}, {10, 10}}, opts);
        QVERIFY(plateau.valid);
        QVERIFY(std::abs(plateau.integralRawEcG - 125.0) < 1e-6);

        opts.activeStopTimeSec = 5.0;
        const auto linear = PortalTailAnalyzer::analyze(portal, {{0, 0}, {10, 30}}, opts);
        QVERIFY(linear.valid);
        QVERIFY(std::abs(linear.integralRawEcG - 450.0) < 1e-6);
        QVERIFY(std::abs(linear.activeIntegralRawEcG - 262.5) < 1e-6);
        QVERIFY(std::abs(linear.tailMean5g - (10.0 + 5.0 / 6.0)) < 1e-6);
        opts.lagOffsetSec = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!PortalTailAnalyzer::analyze(portal, {{0, 0}, {10, 30}}, opts).valid);
    }

    void portalIntegrationNeverBridgesARecordedInterruption() {
        QVector<PortalSample> portal;
        for (int i = 0; i <= 100; ++i) {
            if (i > 39 && i < 60) continue;
            portal.append({i / 10.0, 8.0, 55.0, i == 0 || i == 60});
        }
        PortalTailAnalyzer::Options opts;
        opts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
        const auto gap = PortalTailAnalyzer::analyze(
            portal, {{0, 0}, {3.9, 3.9}, {6, 6}, {10, 10}}, opts);
        QVERIFY(!gap.valid);
        QCOMPARE(gap.status, PortalTailAnalyzer::Status::InsufficientCoverage);
        QVERIFY(gap.coveredMassG <= 7.9 + 1e-6);

        portal.clear();
        QVector<QPointF> weight;
        for (int i = 0; i <= 100; ++i) {
            portal.append({i / 10.0, 8.0, 55.0, i == 0 || i == 96});
            weight.append({i / 10.0, i * 0.3});
        }
        const auto tailGap = PortalTailAnalyzer::analyze(portal, weight, opts);
        QVERIFY(!tailGap.valid);
        QCOMPARE(tailGap.status, PortalTailAnalyzer::Status::InsufficientCoverage);
    }

    void portalCalibrationNeverUsesAnotherBeanOrItsOwnTarget() {
        PortalTailAnalyzer::CalibrationPoint point;
        point.shotId = 7;
        point.bagId = 2;
        point.beanBrand = QStringLiteral("Roaster");
        point.beanType = QStringLiteral("Coffee A");
        point.meanRawEcPerG = 8;
        point.measuredTdsPct = 10;
        point.estimateReliable = true;
        point.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
        const auto unrelated = PortalTailAnalyzer::fitCalibration(
            {point}, 3, QStringLiteral("Roaster"), QStringLiteral("Coffee B"), 8, PortalTailAnalyzer::MountingMode::OnScale);
        QVERIFY(!unrelated.calibrated);
        const auto self = PortalTailAnalyzer::fitCalibration(
            {point}, 2, point.beanBrand, point.beanType, 7, PortalTailAnalyzer::MountingMode::OnScale);
        QVERIFY(!self.calibrated);
        const auto otherMount = PortalTailAnalyzer::fitCalibration(
            {point}, 2, point.beanBrand, point.beanType, 8, PortalTailAnalyzer::MountingMode::Independent);
        QVERIFY(!otherMount.calibrated);
        point.estimateReliable = false;
        const auto unreliable = PortalTailAnalyzer::fitCalibration(
            {point}, 2, point.beanBrand, point.beanType, 8, PortalTailAnalyzer::MountingMode::OnScale);
        QVERIFY(!unreliable.calibrated);
        QCOMPARE(PortalTailAnalyzer::weightSourceFromString({}),
                 PortalTailAnalyzer::WeightSource::Unknown);
    }

    void portalReferencesRequireTheSameSetupAndCurrentTaste() {
        PortalTailAnalyzer::Result result;
        result.valid = true;
        result.estimateReliable = true;
        result.totalMassG = 24;
        result.tailRatio5g = 0.5;
        PortalTailAnalyzer::CalibrationPoint other;
        other.shotId = 1;
        other.bagId = 7;
        other.equipmentId = 3;
        other.profileName = QStringLiteral("A-Flow");
        other.doseWeightG = 16;
        other.finalWeightG = 32;
        other.enjoyment = 95;
        other.tasteBalance = QStringLiteral("sour");
        PortalTailAnalyzer::evaluateRatioAdvice(&result, {other}, 2, 7, {}, {}, 16,
            0, {}, {}, QStringLiteral("not sour"), 4, QStringLiteral("A-Flow"));
        QCOMPARE(result.bestHistoricalWeightG, 0.0);
        QVERIFY(!result.possibleUnderExtraction);
        PortalTailAnalyzer::evaluateRatioAdvice(&result, {other}, 2, 7, {}, {}, 16,
            0, {}, {}, {}, 3, QStringLiteral("A-Flow"));
        QCOMPARE(result.bestHistoricalWeightG, 32.0);
        QVERIFY(!result.possibleUnderExtraction);
        PortalTailAnalyzer::evaluateRatioAdvice(&result, {other}, 1, 7, {}, {}, 16,
            0, {}, {}, {}, 3, QStringLiteral("A-Flow"));
        QCOMPARE(result.bestHistoricalWeightG, 0.0);
    }

    void portalTailAnalyzerIntegratesMassAndTailFeaturesExactly() {
        // Piecewise linear EC(m) with known analytical integral:
        //   m in [0, 10] g (t in [5, 15] s): EC = 10.0 raw-EC -> integral = 100.0
        //   m in [10, 30] g (t in [15, 35] s): EC decays linearly from 10.0 to 2.0
        //     at slope -0.4 raw-EC/g -> integral = 0.5 * (10 + 2) * 20 = 120.0
        // Total integral = 220.0 raw-EC * g over 30.0 g (mean = 220 / 30 = 7.333333).
        // Over final 5 g [25, 30] g: EC goes from 4.0 to 2.0 -> M5 = 3.0, T5 = -0.4,
        // R5 = 3.0 / 10.0 = 0.3, recentContributionShare5g = 15.0 / 220.0.
        // Over final 2 g [28, 30] g: EC goes from 2.8 to 2.0 -> M2 = 2.4.
        QVector<PortalSample> portal;
        for (int i = 0; i <= 350; ++i) {
            const double t = i * 0.1;
            double ec = 10.0;
            if (t > 15.0)
                ec = std::max(2.0, 10.0 - 0.4 * (t - 15.0));
            portal.append(PortalSample{t, ec, 60.0, i == 0});
        }

        // Include realistic scale traits from hardware commissioning:
        //  1) First recorded weight starts at 0.2 g (at t = 5.2 s) because <0.1 g is gated.
        //  3) Final SAW settling point at t = 35.04 s (+40 ms after the last PORTAL sample at 35.0 s).
        QVector<QPointF> weight;
        weight.append(QPointF(5.2, 0.2));
        for (int s = 6; s <= 34; ++s) {
            const double m = static_cast<double>(s - 5);
            weight.append(QPointF(static_cast<double>(s), m));
        }
        weight.append(QPointF(35.0, 30.0));
        weight.append(QPointF(35.04, 30.0)); // scale confirms the settled plateau

        PortalTailAnalyzer::Options opts;
        opts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
        opts.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
        opts.doseWeightG = 16.0;
        opts.activeStopTimeSec = 33.0; // active pour stops at m = 28 g, last 2 g is SAW drip

        const auto res = PortalTailAnalyzer::analyze(portal, weight, opts);
        QVERIFY(res.valid);
        QCOMPARE(res.status, PortalTailAnalyzer::Status::Valid);
        QCOMPARE(res.calibrationMode, QStringLiteral("relative_proxy_only"));
        QCOMPARE(res.gapCount, 0);
        QVERIFY(std::abs(res.totalMassG - 30.0) < 1e-6);
        QVERIFY(std::abs(res.coverageRatio - 1.0) < 1e-6);
        QVERIFY(std::abs(res.integralRawEcG - 220.0) < 1e-4);
        QVERIFY(std::abs(res.activeIntegralRawEcG - (220.0 - 4.8)) < 1e-4);
        QVERIFY(std::abs(res.settlingIntegralRawEcG - 4.8) < 1e-4);
        QVERIFY(std::abs(res.meanRawEcPerG - (220.0 / 30.0)) < 1e-4);
        QVERIFY(std::abs(res.doseNormalizedIntegral - (220.0 / 16.0)) < 1e-4);
        QVERIFY(std::abs(res.tailMean2g - 2.4) < 1e-4);
        QVERIFY(std::abs(res.tailMean5g - 3.0) < 1e-4);
        QVERIFY(std::abs(res.tailSlope5gPerG - (-0.4)) < 1e-4);
        QVERIFY(std::abs(res.tailRatio5g - 0.3) < 1e-4);
        QVERIFY(std::abs(res.recentContributionShare5g - (15.0 / 220.0)) < 1e-4);
        QCOMPARE(res.instabilityDipCount, 0);

        const QVariantMap map = res.toVariantMap();
        QCOMPARE(map.value("valid").toBool(), true);
        QCOMPARE(map.value("calibrationMode").toString(), QStringLiteral("relative_proxy_only"));
        QCOMPARE(map.value("mountingMode").toString(), QStringLiteral("onScale"));
        QCOMPARE(map.value("weightSource").toString(), QStringLiteral("physical_scale"));
    }

    void portalTailAnalyzerEnforcesPhysicalScaleAndCoverageGates() {
        QVector<PortalSample> portal;
        for (int i = 0; i <= 50; ++i) {
            const double t = i * 0.2;
            portal.append(PortalSample{t, 8.0 - 0.4 * t, 55.0, i == 0});
        }
        QVector<QPointF> weight{
            {1.0, 1.0},
            {5.0, 15.0},
            {10.0, 30.0}
        };

        // 1. FlowScaleVirtual must be rejected
        PortalTailAnalyzer::Options flowOpts;
        flowOpts.weightSource = PortalTailAnalyzer::WeightSource::FlowScaleVirtual;
        const auto flowRes = PortalTailAnalyzer::analyze(portal, weight, flowOpts);
        QVERIFY(!flowRes.valid);
        QCOMPARE(flowRes.status, PortalTailAnalyzer::Status::RequiresPhysicalScale);

        // 2. Total mass < 5.0 g must be rejected
        PortalTailAnalyzer::Options shortOpts;
        shortOpts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
        const QVector<QPointF> shortWeight{{1.0, 1.0}, {4.0, 4.5}};
        const auto shortRes = PortalTailAnalyzer::analyze(portal, shortWeight, shortOpts);
        QVERIFY(!shortRes.valid);
        QCOMPARE(shortRes.status, PortalTailAnalyzer::Status::InsufficientWeightData);

        // 3. Internal BLE drop (breakBefore / gap > 0.5 s) leaves middle mass steps uncovered
        QVector<PortalSample> gappyPortal;
        for (int i = 0; i <= 10; ++i)
            gappyPortal.append(PortalSample{i * 0.2, 10.0, 50.0, i == 0});
        // Single BLE drop from t = 2.0 s to t = 8.0 s while 20 g of coffee lands in the cup
        for (int i = 40; i <= 50; ++i)
            gappyPortal.append(PortalSample{i * 0.2, 3.0, 58.0, i == 40});

        const QVector<QPointF> denseWeight{
            {1.0, 2.0},
            {2.0, 5.0},
            {5.0, 18.0},
            {8.0, 25.0},
            {10.0, 30.0}
        };
        const auto gapRes = PortalTailAnalyzer::analyze(gappyPortal, denseWeight, shortOpts);
        QVERIFY(!gapRes.valid);
        QCOMPARE(gapRes.status, PortalTailAnalyzer::Status::InsufficientCoverage);
        QCOMPARE(gapRes.gapCount, 1);
        QVERIFY(gapRes.coverageRatio < PortalTailAnalyzer::kMinCoverageRatio);

        shortOpts.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
        const QVector<PortalSample> latePortal{{10.0, 8.0, 55.0, true}, {10.2, 8.0, 55.0, false}};
        const auto lateRes = PortalTailAnalyzer::analyze(latePortal, {{10.0, 25.0}, {10.2, 25.0}}, shortOpts);
        QVERIFY(!lateRes.valid);
        QVERIFY(!lateRes.estimateReliable);
        QCOMPARE(lateRes.status, PortalTailAnalyzer::Status::InsufficientCoverage);
        QCOMPARE(lateRes.coveredMassG, 0.0);
        QCOMPARE(lateRes.integralRawEcG, 0.0);

        const auto partialRes = PortalTailAnalyzer::analyze(latePortal, {{10.0, 25.0}, {10.2, 30.0}}, shortOpts);
        QVERIFY(!partialRes.valid);
        QCOMPARE(partialRes.status, PortalTailAnalyzer::Status::InsufficientCoverage);
        QCOMPARE(partialRes.coveredMassG, 5.0);
        QCOMPARE(partialRes.integralRawEcG, 40.0);
    }

    void portalTailAnalyzerDetectsMidShotVDipAndComputesLagSensitivity() {
        // Construct a curve with a main peak at m = 5 g (EC = 20), a sharp V-dip
        // at m = 12 g (EC drops from 14 to 9 and recovers to 12.5 at m = 15 g),
        // followed by normal tail decay to EC = 3 at m = 32 g.
        auto ecForMass = [](double m) {
            if (m < 5.0)
                return 4.0 * m;
            if (m >= 10.0 && m <= 12.0)
                return 14.0 - 2.5 * (m - 10.0); // drops 14 -> 9
            if (m > 12.0 && m <= 15.0)
                return 9.0 + (3.5 / 3.0) * (m - 12.0); // recovers 9 -> 12.5
            return 20.0 - 0.55 * (m - 5.0);
        };

        QVector<PortalSample> portal;
        for (int i = 0; i <= 330; ++i) {
            const double t = i * 0.1; // 10 Hz PORTAL stream
            portal.append(PortalSample{t, ecForMass(t), 60.0, i == 0});
        }

        QVector<QPointF> weight;
        for (int i = 1; i <= 32; ++i) {
            const double t = static_cast<double>(i);
            weight.append(QPointF(t, t));
        }

        PortalTailAnalyzer::Options opts;
        opts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
        opts.doseWeightG = 16.0;

        const auto res = PortalTailAnalyzer::analyze(portal, weight, opts);
        QVERIFY(res.valid);
        QCOMPARE(res.instabilityDipCount, 1);
        QVERIFY(res.maxDipProminenceRawEc >= 3.0);
        QCOMPARE(res.instabilityDips.first().valleyMassG, 12.0);

        const auto lagTable = PortalTailAnalyzer::analyzeLagSensitivity(portal, weight, opts);
        QCOMPARE(lagTable.size(), 9);
        for (const auto& row : lagTable) {
            QVERIFY(row.valid);
            QVERIFY(row.integralRawEcG > 0.0);
        }
    }

    void portalAnalysisFlowsThroughProjectionMcpAndAiSummarizer() {
        // 2. Non-PORTAL ShotRecord omits portalAnalysis from ShotProjection::toVariantMap()
        ShotRecord plainRecord;
        plainRecord.summary.id = 101;
        plainRecord.summary.doseWeight = 16.0;
        plainRecord.summary.finalWeight = 32.0;
        plainRecord.summary.duration = 25.0;
        plainRecord.pressure = {{0.0, 1.0}, {22.0, 6.0}};
        plainRecord.flow = {{0.0, 2.0}, {22.0, 1.8}};
        plainRecord.weight = {{5.0, 0.5}, {25.0, 32.0}};

        const ShotProjection plainProj = ShotHistoryStorage::convertShotRecord(plainRecord);
        QVERIFY(plainProj.portalAnalysis.isEmpty());
        QVERIFY(!plainProj.toVariantMap().contains(QStringLiteral("portalAnalysis")));

        // 3. PORTAL ShotRecord populates ShotProjection::portalAnalysis, survives
        //    toVariantMap()/fromVariantMap() and MCP summary stripping, and flows
        //    into ShotSummarizer structured JSON with relative_proxy_only framing.
        ShotRecord portalRecord = plainRecord;
        portalRecord.summary.id = 102;
        portalRecord.weightSource = QStringLiteral("physical_scale");
        portalRecord.portalMountingMode = QStringLiteral("onScale");
        portalRecord.debugLog = QStringLiteral("[SAW][Learning] Model: source=perProfile scale=skale basket=vst-15-g");
        for (int i = 0; i <= 250; ++i) {
            const double t = i * 0.1;
            const double ec = (t <= 15.0) ? 10.0 : std::max(2.5, 10.0 - 0.5 * (t - 15.0));
            portalRecord.portalSamples.append(PortalSample{t, ec, 62.0, i == 0});
        }
        portalRecord.weight.clear();
        for (int s = 5; s <= 25; ++s) {
            const double m = (s - 5) * 1.6; // 0.0 g at 5s -> 32.0 g at 25s
            if (m >= 0.2)
                portalRecord.weight.append(QPointF(static_cast<double>(s), m));
        }

        const ShotProjection portalProj = ShotHistoryStorage::convertShotRecord(portalRecord);
        QVERIFY(!portalProj.portalAnalysis.isEmpty());
        QCOMPARE(portalProj.portalAnalysis.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(portalProj.portalAnalysis.value(QStringLiteral("calibrationMode")).toString(),
                 QStringLiteral("relative_proxy_only"));
        QCOMPARE(portalProj.portalAnalysis.value(QStringLiteral("weightSource")).toString(),
                 QStringLiteral("physical_scale"));
        auto unclassified = portalRecord;
        unclassified.weightSource.clear();
        QVERIFY(!ShotHistoryStorage::convertShotRecord(unclassified).portalAnalysis.value("valid").toBool());
        QVERIFY(portalProj.portalAnalysis.value(QStringLiteral("integralRawEcG")).toDouble() > 200.0);

        // Round-trip through QVariantMap
        const QVariantMap projMap = portalProj.toVariantMap();
        QVERIFY(projMap.contains(QStringLiteral("portalAnalysis")));
        const ShotProjection roundTripped = ShotProjection::fromVariantMap(projMap);
        QCOMPARE(roundTripped.portalAnalysis.value(QStringLiteral("integralRawEcG")).toDouble(),
                 portalProj.portalAnalysis.value(QStringLiteral("integralRawEcG")).toDouble());

        // MCP detail='summary' stripTimeSeriesFields drops portalSamples array but keeps portalAnalysis object
        QJsonObject mcpSummary = portalProj.toJsonObject();
        McpShotsHelpers::stripTimeSeriesFields(mcpSummary);
        QVERIFY(!mcpSummary.contains(QStringLiteral("portalSamples")));
        QVERIFY(mcpSummary.contains(QStringLiteral("portalAnalysis")));
        QCOMPARE(mcpSummary.value(QStringLiteral("portalAnalysis")).toObject()
                     .value(QStringLiteral("calibrationMode")).toString(),
                 QStringLiteral("relative_proxy_only"));

        // AI ShotSummarizer structured JSON & prose
        ShotSummarizer summarizer;
        const ShotSummary aiSummary = summarizer.summarizeFromHistory(portalProj);
        QVERIFY(!aiSummary.portalAnalysis.isEmpty());
        const QJsonObject promptObj = summarizer.buildUserPromptObject(aiSummary);
        const QJsonObject shotBlock = promptObj.value(QStringLiteral("shot")).toObject();
        QVERIFY(shotBlock.contains(QStringLiteral("portal")));
        const QJsonObject portalBlock = shotBlock.value(QStringLiteral("portal")).toObject();
        QCOMPARE(portalBlock.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(portalBlock.value(QStringLiteral("calibrationMode")).toString(),
                 QStringLiteral("relative_proxy_only"));
        QVERIFY(portalBlock.value(QStringLiteral("integralRawEcG")).toDouble() > 200.0);
        QVERIFY(portalBlock.contains(QStringLiteral("M2")));
        QVERIFY(portalBlock.contains(QStringLiteral("M5")));
        QVERIFY(portalBlock.contains(QStringLiteral("T5")));
        QVERIFY(portalBlock.contains(QStringLiteral("R5")));
        QVERIFY(portalBlock.contains(QStringLiteral("recentContributionShare5g")));

        const QString prose = promptObj.value(QStringLiteral("shotAnalysis")).toString();
        QVERIFY(!prose.contains(QStringLiteral("Belka PORTAL")));
    }
    void portalMountingAndRefractometerCalibration()
    {
        // Mounting geometry setting
        Settings settings;
        QCOMPARE(settings.hardware()->portalMountingMode(), QStringLiteral("onScale"));
        settings.hardware()->setPortalMountingMode(QStringLiteral("independent"));
        QCOMPARE(settings.hardware()->portalMountingMode(), QStringLiteral("independent"));
        settings.hardware()->setPortalMountingMode(QStringLiteral("invalid_mode"));
        QCOMPARE(settings.hardware()->portalMountingMode(), QStringLiteral("onScale"));

        // 2. 4-step empirical refractometer (Atago PAL-COFFEE TDS) calibration & validation
        //    Step 1: 5 shots at yields 24, 28, 32, 36, 40 g (dose 16.0 g)
        QVector<PortalTailAnalyzer::CalibrationPoint> calPoints;
        const QVector<double> yields = {24.0, 28.0, 32.0, 36.0, 40.0};
        const QVector<double> meanEcs = {9.60, 8.50, 7.60, 6.90, 6.30};
        const double trueK = 1.25; // TDS% = 1.25 * meanRawEcPerG
        for (int i = 0; i < yields.size(); ++i) {
            PortalTailAnalyzer::CalibrationPoint pt;
            pt.shotId = 200 + i;
            pt.estimateReliable = true;
            pt.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
            pt.bagId = 7;
            pt.beanBrand = QStringLiteral("La Cabra");
            pt.beanType = QStringLiteral("Cerro Azul");
            pt.doseWeightG = 16.0;
            pt.finalWeightG = yields[i];
            pt.meanRawEcPerG = meanEcs[i];
            pt.integralRawEcG = meanEcs[i] * yields[i];
            pt.meanTempC = 60.0 + 0.5 * i;
            pt.measuredTdsPct = trueK * meanEcs[i];
            pt.measuredEyPct = (pt.measuredTdsPct * pt.finalWeightG) / pt.doseWeightG;
            calPoints.append(pt);
        }

        // Step 2 & 3: Fit calibration on the 5-shot bag series for a 6th validation shot (shotId=299)
        const auto fit = PortalTailAnalyzer::fitCalibration(
            calPoints, 7, QStringLiteral("La Cabra"), QStringLiteral("Cerro Azul"), 299, PortalTailAnalyzer::MountingMode::OnScale);
        QVERIFY(fit.calibrated);
        QCOMPARE(fit.scope, QStringLiteral("bag"));
        QCOMPARE(fit.shotCount, 5);
        QCOMPARE(fit.activeModel, QStringLiteral("proportional"));
        QVERIFY(std::abs(fit.proportionalK - trueK) < 1e-4);
        QVERIFY(fit.proportionalLoocvMaeTdsPct < 0.05);

        // Step 4: Validate on an out-of-sample shot (meanRawEcPerG = 7.62, yield = 32.1 g, dose = 16.0 g,
        //         measured Atago TDS = 9.45%)
        PortalTailAnalyzer::Result valRes;
        valRes.valid = true;
        valRes.estimateReliable = true;
        valRes.status = PortalTailAnalyzer::Status::Valid;
        valRes.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
        valRes.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
        valRes.portalSampleCount = 120;
        valRes.totalMassG = 32.1;
        valRes.coveredMassG = 31.8;
        valRes.coverageRatio = 31.8 / 32.1;
        valRes.meanRawEcPerG = 7.62;
        valRes.integralRawEcG = 7.62 * 32.1;
        valRes.meanTempC = 61.4;

        PortalTailAnalyzer::applyCalibration(&valRes, fit, 32.1, 16.0, 9.45);
        QVERIFY(valRes.calibrated);
        QCOMPARE(valRes.calibrationScope, QStringLiteral("bag"));
        QCOMPARE(valRes.calibrationShotCount, 5);
        const double expectedEstTds = trueK * 7.62; // 9.525%
        QVERIFY(std::abs(valRes.estimatedTdsPct - expectedEstTds) < 1e-3);
        const double expectedEstEy = (expectedEstTds * 32.1) / 16.0; // 19.1095%
        QVERIFY(std::abs(valRes.estimatedEyPct - expectedEstEy) < 1e-2);
        QVERIFY(valRes.hasValidationReading);
        QVERIFY(std::abs(valRes.validationResidualTdsPct - (expectedEstTds - 9.45)) < 1e-3);
        QCOMPARE(fit.minRatio, 1.5);
        QCOMPARE(fit.maxRatio, 2.5);
        auto extrapolated = valRes;
        extrapolated.meanRawEcPerG = 12.0;
        PortalTailAnalyzer::applyCalibration(&extrapolated, fit, 20.0, 16.0);
        QVERIFY(extrapolated.calibrated);
        QVERIFY(!extrapolated.estimateReliable);
        QVERIFY(extrapolated.uncertaintyReasons.contains(QStringLiteral("outside_calibration_range")));

        const QVector<QPointF> incompleteMeasurements{
            {0.0, 32.0}, {16.0, 0.0},
            {16.0, std::numeric_limits<double>::infinity()},
            {16.0, std::numeric_limits<double>::quiet_NaN()}
        };
        for (const auto& measurement : incompleteMeasurements) {
            auto references = calPoints;
            auto incomplete = calPoints.first();
            incomplete.shotId = 300;
            incomplete.doseWeightG = measurement.x();
            incomplete.finalWeightG = measurement.y();
            references.prepend(incomplete);
            const auto partialFit = PortalTailAnalyzer::fitCalibration(references, 7,
                QStringLiteral("La Cabra"), QStringLiteral("Cerro Azul"), 299,
                PortalTailAnalyzer::MountingMode::OnScale);
            QVERIFY(partialFit.calibrated);
            QCOMPARE(partialFit.shotCount, 6);
            QCOMPARE(partialFit.minRatio, 1.5);
            QCOMPARE(partialFit.maxRatio, 2.5);
            auto ratioExtrapolated = valRes;
            PortalTailAnalyzer::applyCalibration(&ratioExtrapolated, partialFit, 20.0, 16.0);
            QVERIFY(!ratioExtrapolated.estimateReliable);
            QVERIFY(ratioExtrapolated.uncertaintyReasons.contains(QStringLiteral("outside_calibration_range")));
        }

        // 3. End-to-end ShotHistoryStorage persistence & calibration lookup
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString dbPath = tempDir.filePath(QStringLiteral("portal_cal_test.db"));
        ShotHistoryStorage storage;
        QVERIFY(storage.initialize(dbPath));

        auto makePortalRecord = [](const QString& uuid, qint64 ts, double yieldG, double ecLevel, double tdsPct) {
            ShotRecord rec;
            rec.summary.uuid = uuid;
            rec.summary.timestamp = ts;
            rec.summary.profileName = QStringLiteral("Londinium R");
            rec.summary.doseWeight = 16.0;
            rec.summary.finalWeight = yieldG;
            rec.summary.duration = 25.0;
            rec.summary.beanBrand = QStringLiteral("La Cabra");
            rec.summary.beanType = QStringLiteral("Cerro Azul");
            rec.drinkTds = tdsPct;
            rec.drinkEy = tdsPct > 0.0 ? (tdsPct * yieldG / 16.0) : 0.0;
            rec.weightSource = QStringLiteral("physical_scale");
            rec.portalMountingMode = QStringLiteral("onScale");
            rec.debugLog = QStringLiteral("[SAW][Learning] scale=acaia\n[BLE Portal] Shot capture started (mountingMode=onScale)");
            rec.pressure = {{0.0, 2.0}, {10.0, 9.0}, {25.0, 5.0}};
            rec.flow = {{0.0, 2.0}, {10.0, 1.5}, {25.0, 1.5}};
            for (int s = 5; s <= 25; ++s) {
                const double m = (s - 5) * (yieldG / 20.0);
                if (m >= 0.2)
                    rec.weight.append(QPointF(static_cast<double>(s), m));
            }
            for (int i = 0; i <= 250; ++i) {
                const double t = i * 0.1;
                rec.portalSamples.append(PortalSample{t, ecLevel, 61.5, i == 0});
            }
            return rec;
        };

        // Import 3 calibrated reference shots and 1 unmeasured target shot
        const qint64 id1 = storage.importShotRecord(makePortalRecord(QStringLiteral("u-1"), 1700000100, 28.0, 8.0, 10.0), true);
        const qint64 id2 = storage.importShotRecord(makePortalRecord(QStringLiteral("u-2"), 1700000200, 32.0, 7.2, 9.0), true);
        const qint64 id3 = storage.importShotRecord(makePortalRecord(QStringLiteral("u-3"), 1700000300, 36.0, 6.4, 8.0), true);
        const qint64 id4 = storage.importShotRecord(makePortalRecord(QStringLiteral("u-4"), 1700000400, 32.0, 7.6, 0.0), true);
        QVERIFY(id1 > 0 && id2 > 0 && id3 > 0 && id4 > 0);

        auto hotRecord = makePortalRecord(QStringLiteral("u-hot"), 1700000500, 32.0, 7.0, 12.0);
        hotRecord.espressoNotes = QStringLiteral("My tasting notes");
        for (auto& reading : hotRecord.portalSamples) reading.temperatureC = 72.0;
        const qint64 hotId = storage.importShotRecord(hotRecord, true);
        QVERIFY(hotId > 0);

        // Newer measurements from other coffees cannot evict this coffee's references.
        withTempDb(dbPath, "portal_other_coffee", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            QVERIFY(q.exec(R"(
                WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<201)
                INSERT INTO shots(uuid,timestamp,profile_name,duration_seconds,bean_brand,bean_type,
                                  dose_weight,final_weight,drink_tds,portal_mean_ec,portal_estimate_reliable,
                                  portal_mounting_mode,weight_source,portal_present)
                SELECT 'other-'||i,1800000000+i,'Espresso',25,'Other','Coffee',16,32,10,8,1,
                       'onScale','physical_scale',1 FROM n
            )"));
        });
        ShotRecord loaded4;
        withTempDb(dbPath, "portal_cal_reload", [&](QSqlDatabase& db) {
            QSqlQuery measurement(db);
            measurement.prepare("UPDATE shots SET drink_tds=20.0 WHERE id=?");
            measurement.addBindValue(id4);
            QVERIFY(measurement.exec());
            loaded4 = ShotHistoryStorage::loadShotRecordStatic(db, id4);
            const auto hotLoaded = ShotHistoryStorage::loadShotRecordStatic(db, hotId);
            QCOMPARE(hotLoaded.espressoNotes, QStringLiteral("My tasting notes"));
            QSqlQuery notes(db);
            notes.prepare("SELECT espresso_notes FROM shots WHERE id=?");
            notes.addBindValue(hotId);
            QVERIFY(notes.exec() && notes.next());
            QCOMPARE(notes.value(0).toString(), QStringLiteral("My tasting notes"));
            const auto outcome = ShotHistoryStorage::shotOutcomeStatic(db, id4);
            const auto updatedPortal = outcome.value("portalAnalysis").toMap();
            QCOMPARE(updatedPortal.value("calibrationShotCount").toInt(), 3);
            QVERIFY(std::abs(updatedPortal.value("estimatedTdsPct").toDouble() - 9.5) < 0.05);
        });
        // The real metadata-save path must change the model returned to Settings.
        // This target's deliberately wrong 20% TDS is corrected to 9.5%, giving
        // four independent pairs on TDS = 1.25 * EC; the hot pair stays excluded.
        QSignalSpy metadataSaved(&storage, &ShotHistoryStorage::shotMetadataUpdated);
        QSignalSpy calibrationReady(&storage, &ShotHistoryStorage::portalCalibrationReady);
        storage.requestUpdateShotMetadata(id4, {{QStringLiteral("drinkTds"), 9.5}});
        QTRY_COMPARE(metadataSaved.size(), 1);
        QVERIFY(metadataSaved.first().at(1).toBool());
        storage.requestPortalCalibration(QStringLiteral("after-save"), -1,
            QStringLiteral("La Cabra"), QStringLiteral("Cerro Azul"), PortalTailAnalyzer::MountingMode::OnScale);
        QTRY_COMPARE(calibrationReady.size(), 1);
        QCOMPARE(calibrationReady.first().at(0).toString(), QStringLiteral("after-save"));
        QVERIFY(calibrationReady.first().at(2).toString().isEmpty());
        const auto refreshed = calibrationReady.first().at(1).toMap();
        QCOMPARE(refreshed.value("shotCount").toInt(), 4);
        QCOMPARE(refreshed.value("excludedShotCount").toInt(), 1);
        QVERIFY(std::abs(refreshed.value("proportionalK").toDouble() - 1.25) < 0.001);
        const auto references = refreshed.value("referenceShots").toList();
        QCOMPARE(references.size(), 5);
        for (const auto& reference : references) {
            const auto row = reference.toMap();
            QCOMPARE(row.value("included").toBool(), row.value("shotId").toLongLong() != hotId);
        }
        storage.requestUpdateShotMetadata(id4, {{QStringLiteral("drinkTds"), 0.0}});
        QTRY_COMPARE(metadataSaved.size(), 2);
        QVERIFY(metadataSaved.last().at(1).toBool());
        storage.requestPortalCalibration(QStringLiteral("after-clear"), -1,
            QStringLiteral("La Cabra"), QStringLiteral("Cerro Azul"), PortalTailAnalyzer::MountingMode::OnScale);
        QTRY_COMPARE(calibrationReady.size(), 2);
        QCOMPARE(calibrationReady.last().at(0).toString(), QStringLiteral("after-clear"));
        QCOMPARE(calibrationReady.last().at(1).toMap().value("shotCount").toInt(), 3);
        auto warmRecord = makePortalRecord(QStringLiteral("warm"), 1700000600, 32.0, 7.0, 8.75);
        for (auto& reading : warmRecord.portalSamples) reading.temperatureC = 65.0;
        const qint64 warmId = storage.importShotRecord(warmRecord, true);
        QVERIFY(warmId > 0);
        storage.requestUpdateShotMetadata(warmId, {{QStringLiteral("doseWeight"), 12.0}});
        QTRY_COMPARE(metadataSaved.size(), 3);
        QVERIFY(metadataSaved.last().at(1).toBool());
        withTempDb(dbPath, "portal_provenance", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("SELECT portal_estimate_reliable, weight_source, portal_mounting_mode, portal_present FROM shots WHERE id=?");
            q.addBindValue(warmId);
            QVERIFY(q.exec() && q.next());
            QVERIFY(!q.value(0).toBool());
            QCOMPARE(q.value(1).toString(), QStringLiteral("physical_scale"));
            QCOMPARE(q.value(2).toString(), QStringLiteral("onScale"));
            QVERIFY(q.value(3).toBool());
            const auto outcome = ShotHistoryStorage::shotOutcomeStatic(db, warmId);
            QVERIFY(outcome.value("portalAnalysis").toMap().value("thermalDriftWarning").toBool());
        });
        storage.requestUpdateShotMetadata(warmId, {{QStringLiteral("doseWeight"), 16.0}});
        QTRY_COMPARE(metadataSaved.size(), 4);
        QVERIFY(metadataSaved.last().at(1).toBool());
        auto virtualRecord = warmRecord;
        virtualRecord.summary.uuid = QStringLiteral("virtual-saw-physical-key");
        virtualRecord.summary.timestamp += 60;
        virtualRecord.weightSource = QStringLiteral("flow_scale_virtual");
        const qint64 virtualId = storage.importShotRecord(virtualRecord, true);
        QVERIFY(virtualId > 0);
        withTempDb(dbPath, "portal_virtual", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            q.prepare("SELECT portal_estimate_reliable FROM shots WHERE id=?");
            q.addBindValue(warmId);
            QVERIFY(q.exec() && q.next());
            QVERIFY(q.value(0).toBool());
            const auto loaded = ShotHistoryStorage::loadShotRecordStatic(db, virtualId);
            QCOMPARE(loaded.weightSource, QStringLiteral("flow_scale_virtual"));
            QCOMPARE(ShotHistoryStorage::convertShotRecord(loaded).portalAnalysis.value("status").toString(),
                     QStringLiteral("requires_physical_scale"));
        });
        ShotDataModel captured;
        captured.setPortalMountingMode(QStringLiteral("onScale"));
        captured.recordWeightSource(QStringLiteral("physical_scale"));
        for (int i = 0; i <= 250; ++i) {
            const double time = i * 0.1;
            captured.addSample(time, 9.0, 2.0, 93.0, 88.0, 9.0, 0.0, 93.0, 94.0);
            captured.addWeightSample(time, time * 1.28, 1.28);
            captured.addPortalSample(time, 7.0, 72.0);
        }
        ShotMetadata metadata;
        metadata.espressoNotes = QStringLiteral("My own tasting notes");
        QSignalSpy shotSaved(&storage, &ShotHistoryStorage::shotSaved);
        QCOMPARE(storage.saveShot(&captured, nullptr, 25.0, 32.0, 16.0, metadata,
            QStringLiteral("scale=acaia"), 0.0, 32.0), 0);
        QTRY_COMPARE(shotSaved.size(), 1);
        const qint64 capturedId = shotSaved.first().at(0).toLongLong();
        QVERIFY(capturedId > 0);
        withTempDb(dbPath, "portal_saved_notes", [&](QSqlDatabase& db) {
            const auto saved = ShotHistoryStorage::loadShotRecordStatic(db, capturedId);
            QCOMPARE(saved.espressoNotes, QStringLiteral("My own tasting notes"));
            QCOMPARE(saved.weightSource, QStringLiteral("physical_scale"));
            QCOMPARE(saved.portalMountingMode, QStringLiteral("onScale"));
        });
        storage.close();
        QTRY_VERIFY(storage.isDbWorkIdle());

        QVERIFY(loaded4.portalMeanEc > 7.5);
        QCOMPARE(loaded4.portalCalibrationPoints.size(), 5);
        const ShotProjection proj4 = ShotHistoryStorage::convertShotRecord(loaded4);
        QCOMPARE(proj4.portalAnalysis.value(QStringLiteral("valid")).toBool(), true);
        QCOMPARE(proj4.portalAnalysis.value(QStringLiteral("mountingMode")).toString(), QStringLiteral("onScale"));
        QCOMPARE(proj4.portalAnalysis.value(QStringLiteral("calibrated")).toBool(), true);
        QCOMPARE(proj4.portalAnalysis.value(QStringLiteral("calibrationScope")).toString(), QStringLiteral("bean"));
        QCOMPARE(proj4.portalAnalysis.value(QStringLiteral("calibrationShotCount")).toInt(), 3);
        QVERIFY(std::abs(proj4.portalAnalysis.value(QStringLiteral("estimatedTdsPct")).toDouble() - 9.5) < 0.05);
        QVERIFY(std::abs(proj4.portalAnalysis.value(QStringLiteral("estimatedEyPct")).toDouble() - 19.0) < 0.15);
        QCOMPARE(proj4.portalAnalysis.value(QStringLiteral("estimateReliable")).toBool(), false);
        QVERIFY(proj4.portalAnalysis.value(QStringLiteral("hasValidationReading")).toBool());
        QVERIFY(std::abs(proj4.portalAnalysis.value(QStringLiteral("validationResidualTdsPct")).toDouble() + 10.5) < 0.05);
        // Verify shot completion / cut-point detection and historical ratio advice on a 1:3 Lungo
        {
            QVector<PortalSample> lungoPortal;
            QVector<QPointF> lungoWeight;
            for (int i = 0; i <= 380; ++i) {
                const double t = i * 0.1;
                const double m = (t <= 5.0) ? 0.0 : ((t - 5.0) / 33.0) * 48.6;
                lungoWeight.append(QPointF(t, m));
                double ec = 0.2;
                if (m >= 0.2) {
                    ec = 19.0 * std::exp(-std::max(0.0, m - 2.6) / 14.5) + 1.4;
                }
                lungoPortal.append(PortalSample{t, ec, 58.0, i == 0});
            }
            PortalTailAnalyzer::Options lOpts;
            lOpts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
            lOpts.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
            lOpts.doseWeightG = 16.0;
            auto lRes = PortalTailAnalyzer::analyze(lungoPortal, lungoWeight, lOpts);
            QVERIFY(lRes.valid);
            QVERIFY(lRes.post1to2MassG > 16.0);
            QVERIFY(lRes.post1to2ContributionShare > 0.05 && lRes.post1to2ContributionShare < 0.20);
            QVERIFY(lRes.massAt90PctIntegralG > 0.0);

            PortalTailAnalyzer::CalibrationPoint bestRef;
            bestRef.shotId = 999;
            bestRef.bagId = 7;
            bestRef.beanBrand = QStringLiteral("April");
            bestRef.beanType = QStringLiteral("Ethiopia");
            bestRef.doseWeightG = 16.0;
            bestRef.finalWeightG = 32.5;
            bestRef.enjoyment = 95;
            bestRef.equipmentId = 3;
            bestRef.profileName = QStringLiteral("A-Flow");
            PortalTailAnalyzer::evaluateRatioAdvice(
                &lRes, {bestRef}, 1000, 7, QStringLiteral("April"), QStringLiteral("Ethiopia"), 16.0, 0, {}, {}, {}, 3, QStringLiteral("A-Flow"));
            QCOMPARE(lRes.bestHistoricalEnjoyment, 95);
            QCOMPARE(lRes.bestHistoricalWeightG, 32.5);
        }

        // Verify potential under-extraction detection when shot is stopped early (24 g on 16 g dose)
        // with high tail concentration and/or user "sour" / under-extracted feedback
        {
            QVector<PortalSample> shortPortal;
            QVector<QPointF> shortWeight;
            for (int i = 0; i <= 220; ++i) {
                const double t = i * 0.1;
                const double m = (t <= 5.0) ? 0.0 : ((t - 5.0) / 17.0) * 24.0;
                shortWeight.append(QPointF(t, m));
                double ec = 0.2;
                if (m >= 0.2) {
                    ec = 17.5 * std::exp(-std::max(0.0, m - 3.5) / 14.0) + 1.8;
                }
                shortPortal.append(PortalSample{t, ec, 54.0, i == 0});
            }
            PortalTailAnalyzer::Options sOpts;
            sOpts.weightSource = PortalTailAnalyzer::WeightSource::PhysicalScale;
            sOpts.mountingMode = PortalTailAnalyzer::MountingMode::OnScale;
            sOpts.doseWeightG = 16.0;
            auto sRes = PortalTailAnalyzer::analyze(shortPortal, shortWeight, sOpts);
            QVERIFY(sRes.valid);
            QCOMPARE(sRes.lowTailConcentration, false);
            QCOMPARE(sRes.possibleUnderExtraction, false);

            PortalTailAnalyzer::CalibrationPoint bestRef;
            bestRef.shotId = 999;
            bestRef.bagId = 7;
            bestRef.beanBrand = QStringLiteral("April");
            bestRef.beanType = QStringLiteral("Ethiopia");
            bestRef.doseWeightG = 16.0;
            bestRef.finalWeightG = 32.5;
            bestRef.enjoyment = 95;
            bestRef.equipmentId = 3;
            bestRef.profileName = QStringLiteral("A-Flow");
            PortalTailAnalyzer::evaluateRatioAdvice(
                &sRes, {bestRef}, 1001, 7, QStringLiteral("April"), QStringLiteral("Ethiopia"),
                16.0, 65, QStringLiteral("sour"), QStringLiteral("thin"), {}, 3, QStringLiteral("A-Flow"));
            QCOMPARE(sRes.possibleUnderExtraction, true);
            QCOMPARE(sRes.ratioAdvice, QStringLiteral("try_longer"));
            QCOMPARE(sRes.bestHistoricalWeightG, 32.5);
        }

    }
};

QTEST_GUILESS_MAIN(tst_BelkaPortal)
#include "tst_belkaportal.moc"
