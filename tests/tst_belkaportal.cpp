#include <QtTest>
#include <QSignalSpy>
#include <QLowEnergyCharacteristic>
#include "ble/belkaportaldevice.h"
#include "ble/belkaportaldiscovery.h"

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
    bool managed = true;
    void setConnectionPriorityManaged(bool value) override { managed = value; }
    void connectToDevice(const QString&, const QString&) override {
        linked = true; operations << "connect"; emit connected();
    }
    void disconnectFromDevice() override {
        linked = false; operations << "disconnect"; emit disconnected();
    }
    void discoverServices() override { operations << "services"; }
    void discoverCharacteristics(const QBluetoothUuid&) override { operations << "characteristics"; }
    void enableNotifications(const QBluetoothUuid&, const QBluetoothUuid& chr) override {
        operations << "subscribe"; emit notificationsEnabled(chr);
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
    void sharedScanKeepsPortalSelectableAfterWifiAndUsbFinish() {
        PortalDiscoveryReplay discovery;
        auto* transport = new PortalTransport;
        BelkaPortalDevice portal(transport);
        connectPortalDiscovery(&discovery, &portal);
        portal.reconnect(); // no saved device: request the common device scan
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
        transport->linked = false;
        emit transport->disconnected();
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
        QCOMPARE(transport->operations, QStringList({"connect", "services", "characteristics", "read", "subscribe"}));
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
        transport->linked = false;
        emit transport->disconnected();
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
};

QTEST_GUILESS_MAIN(tst_BelkaPortal)
#include "tst_belkaportal.moc"
