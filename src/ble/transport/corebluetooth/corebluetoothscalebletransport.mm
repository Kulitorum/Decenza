#include "corebluetoothscalebletransport.h"

#include <QDebug>
#include <QTimer>
#include <QMetaObject>

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
#import <Foundation/Foundation.h>
#import <CoreBluetooth/CoreBluetooth.h>

// ARC compatibility macros
#if __has_feature(objc_arc)
  #define CB_RETAIN(x) (x)
  #define CB_RELEASE(x) do {} while(0)
#else
  #define CB_RETAIN(x) [(x) retain]
  #define CB_RELEASE(x) do { if(x) [(x) release]; } while(0)
#endif

#endif

// ---------- helpers ----------
static inline QString uuidKey(const QBluetoothUuid& s, const QBluetoothUuid& c) {
    return s.toString() + "|" + c.toString();
}

#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
static inline NSString* qsToNs(const QString& s) {
    QByteArray u8 = s.toUtf8();
    return [NSString stringWithUTF8String:u8.constData()];
}

static inline QString nsToQs(NSString* s) {
    return s ? QString::fromUtf8([s UTF8String]) : QString();
}

// CoreBluetooth often gives 16-bit UUID strings like "FF11"
static inline QBluetoothUuid cbUuidToQt(CBUUID* uuid) {
    QString s = nsToQs(uuid.UUIDString).trimmed();
    s.remove('{').remove('}');

    // If it's 4 hex chars, treat as 16-bit UUID
    if (s.size() == 4) {
        bool ok = false;
        quint16 v = s.toUShort(&ok, 16);
        if (ok) return QBluetoothUuid(v);
    }
    // Otherwise, let Qt parse the 128-bit UUID string
    return QBluetoothUuid(s);
}

// Parse UUID from a QString (for use in queued lambdas where we can't access ObjC objects)
static inline QBluetoothUuid uuidFromString(const QString& str) {
    QString s = str.trimmed();
    s.remove('{').remove('}');

    if (s.size() == 4) {
        bool ok = false;
        quint16 v = s.toUShort(&ok, 16);
        if (ok) return QBluetoothUuid(v);
    }
    return QBluetoothUuid(s);
}

static inline int cbPropsToQtProps(CBCharacteristicProperties p) {
    int out = 0;
    if (p & CBCharacteristicPropertyBroadcast) out |= 0x01;
    if (p & CBCharacteristicPropertyRead) out |= 0x02;
    if (p & CBCharacteristicPropertyWriteWithoutResponse) out |= 0x04;
    if (p & CBCharacteristicPropertyWrite) out |= 0x08;
    if (p & CBCharacteristicPropertyNotify) out |= 0x10;
    if (p & CBCharacteristicPropertyIndicate) out |= 0x20;
    if (p & CBCharacteristicPropertyAuthenticatedSignedWrites) out |= 0x40;
    if (p & CBCharacteristicPropertyExtendedProperties) out |= 0x80;
    return out;
}

@class CBDelegateProxy;

struct CoreBluetoothScaleBleTransport::Impl {
    CoreBluetoothScaleBleTransport* q = nullptr;

    CBCentralManager* mgr = nullptr;
    CBDelegateProxy*  del = nullptr;
    CBPeripheral*     periph = nullptr;

    bool connected = false;
    bool servicesDiscovered = false;  // Prevent re-discovery loops
    bool isValid = true;  // Set to false when transport is being destroyed

    QString targetName;
    QString targetUuidString;

    QHash<QBluetoothUuid, CBService*> services;
    QHash<QString, CBCharacteristic*> chars;
    QSet<QBluetoothUuid> charsDiscoveredForService;  // Track which services have had chars discovered

    void log(const QString& m) { if (q && isValid) q->log(m); }

    void clearCaches() {
        services.clear();
        chars.clear();
        charsDiscoveredForService.clear();
        servicesDiscovered = false;
    }

    // Cache just services (called when services are discovered, before characteristics)
    void cacheServices() {
        services.clear();
        if (!periph) return;

        for (CBService* s in periph.services) {
            QBluetoothUuid qs = cbUuidToQt(s.UUID);
            services.insert(qs, s);
        }
        servicesDiscovered = true;
    }

    // Cache services AND characteristics (called when characteristics are discovered)
    void cache() {
        services.clear();
        chars.clear();
        if (!periph) return;

        for (CBService* s in periph.services) {
            QBluetoothUuid qs = cbUuidToQt(s.UUID);
            services.insert(qs, s);

            for (CBCharacteristic* c in s.characteristics) {
                QBluetoothUuid qc = cbUuidToQt(c.UUID);
                chars.insert(uuidKey(qs, qc), c);
            }
        }
        servicesDiscovered = true;
    }

    CBService* findService(const QBluetoothUuid& su) const {
        return services.value(su, nullptr);
    }

    CBCharacteristic* findChar(const QBluetoothUuid& su, const QBluetoothUuid& cu) const {
        return chars.value(uuidKey(su, cu), nullptr);
    }
};

@interface CBDelegateProxy : NSObject<CBCentralManagerDelegate, CBPeripheralDelegate>
@property (nonatomic, assign) CoreBluetoothScaleBleTransport::Impl* impl;
@end

@implementation CBDelegateProxy

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    Q_UNUSED(central);

    auto* d = self.impl;
    if (!d) return;

    // Copy state value NOW, before queuing (don't capture ObjC pointer)
    int state = (int)central.state;
    QMetaObject::invokeMethod(d->q, [d, state]{
        if (!d->isValid) return;
        d->log(QString("Central state=%1").arg(state));

        if (state == CBManagerStatePoweredOn &&
            (!d->targetName.isEmpty() || !d->targetUuidString.isEmpty()) &&
            !d->periph)
        {
            d->log("PoweredOn: attempting connect now");
            d->q->connectToDevice(d->targetUuidString, d->targetName);
        }
    }, Qt::QueuedConnection);
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
     advertisementData:(NSDictionary<NSString *,id> *)advertisementData
                  RSSI:(NSNumber *)RSSI
{
    Q_UNUSED(advertisementData);
    Q_UNUSED(RSSI);

    auto* d = self.impl;
    if (!d) return;

    QString pname = nsToQs(peripheral.name);
    QString pid   = nsToQs(peripheral.identifier.UUIDString);

    bool match = false;
    if (!d->targetUuidString.isEmpty())
        match = (pid.compare(d->targetUuidString, Qt::CaseInsensitive) == 0);

    if (!match && !d->targetName.isEmpty())
        match = (pname == d->targetName) || pname.startsWith(d->targetName);

    if (!match) return;

    // We're already on main thread (CoreBluetooth dispatch queue), so just do CB operations here
    // Then notify Qt thread with copied data only
    d->log(QString("Found target peripheral: %1 (%2)").arg(pname, pid));

    if (central.isScanning) [central stopScan];

    CB_RELEASE(d->periph);
    d->periph = CB_RETAIN(peripheral);
    d->periph.delegate = d->del;

    [central connectPeripheral:d->periph options:nil];
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
    Q_UNUSED(central);
    Q_UNUSED(peripheral);

    auto* d = self.impl;
    if (!d) return;

    // Don't auto-discover services here - let the scale call discoverServices() when ready
    // This prevents duplicate discoveries and gives scales control over timing

    // Notify Qt thread (don't capture ObjC pointers)
    QMetaObject::invokeMethod(d->q, [d]{
        if (!d->isValid) return;
        d->connected = true;
        d->log("Connected!");
        emit d->q->connected();
    }, Qt::QueuedConnection);
}

- (void)centralManager:(CBCentralManager *)central
didDisconnectPeripheral:(CBPeripheral *)peripheral
                 error:(NSError *)error
{
    Q_UNUSED(central);
    Q_UNUSED(peripheral);

    auto* d = self.impl;
    if (!d) return;

    QString reason = error ? nsToQs(error.localizedDescription) : QString("disconnected");
    QMetaObject::invokeMethod(d->q, [d, reason]{
        if (!d->isValid) return;
        d->connected = false;
        d->clearCaches();
        d->log(QString("Disconnected: %1").arg(reason));
        emit d->q->disconnected();
    }, Qt::QueuedConnection);
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    auto* d = self.impl;
    if (!d) return;

    // Ignore if we already processed services (prevents duplicate handling)
    if (d->servicesDiscovered) {
        return;
    }

    // Copy error message NOW, before queuing
    QString errorMsg = error ? nsToQs(error.localizedDescription) : QString();

    // Mark services as discovered and cache them (prevents re-discovery loops)
    if (!error) {
        d->cacheServices();  // This also sets servicesDiscovered = true
    }

    // Copy service UUIDs NOW, before queuing
    QList<QString> serviceUuids;
    if (!error && peripheral.services) {
        for (CBService* s in peripheral.services) {
            serviceUuids.append(nsToQs(s.UUID.UUIDString));
        }
    }

    // Trigger characteristic discovery immediately on main thread (before Qt signals)
    // This ensures characteristics are being discovered while we notify Qt
    if (!error && d->periph) {
        for (CBService* s in d->periph.services) {
            [d->periph discoverCharacteristics:nil forService:s];
        }
    }

    // Now notify Qt thread
    QMetaObject::invokeMethod(d->q, [d, errorMsg, serviceUuids]{
        if (!d->isValid) return;
        if (!errorMsg.isEmpty()) {
            emit d->q->error(QString("Service discovery error: %1").arg(errorMsg));
            return;
        }

        d->log(QString("Discovered %1 services").arg(serviceUuids.size()));

        for (const QString& uuidStr : serviceUuids) {
            QBluetoothUuid su = uuidFromString(uuidStr);
            emit d->q->serviceDiscovered(su);
        }

        emit d->q->servicesDiscoveryFinished();
    }, Qt::QueuedConnection);
}

- (void)peripheral:(CBPeripheral *)peripheral
didDiscoverCharacteristicsForService:(CBService *)service
             error:(NSError *)error
{
    auto* d = self.impl;
    if (!d) return;

    // Get service UUID early to check for duplicates
    QBluetoothUuid serviceUuid = cbUuidToQt(service.UUID);

    // Ignore if we already processed characteristics for this service (prevents duplicates)
    if (d->charsDiscoveredForService.contains(serviceUuid)) {
        return;
    }

    // Mark this service as having its characteristics discovered
    if (!error) {
        d->charsDiscoveredForService.insert(serviceUuid);
    }

    // Copy ALL ObjC data to Qt types NOW, before queuing
    QString errorMsg = error ? nsToQs(error.localizedDescription) : QString();
    QString serviceUuidStr = nsToQs(service.UUID.UUIDString);

    // Collect characteristic info before queuing
    struct CharInfo {
        QString uuidStr;
        int props;
    };
    QList<CharInfo> charInfos;

    if (!error && service.characteristics) {
        for (CBCharacteristic* c in service.characteristics) {
            CharInfo info;
            info.uuidStr = nsToQs(c.UUID.UUIDString);
            info.props = cbPropsToQtProps(c.properties);
            charInfos.append(info);
        }
    }

    QMetaObject::invokeMethod(d->q, [d, errorMsg, serviceUuidStr, charInfos]{
        if (!d->isValid) return;
        if (!errorMsg.isEmpty()) {
            emit d->q->error(QString("Char discovery error: %1").arg(errorMsg));
            return;
        }

        QBluetoothUuid su = uuidFromString(serviceUuidStr);

        d->log(QString("Service %1: %2 characteristics")
               .arg(su.toString()).arg(charInfos.size()));

        for (const auto& info : charInfos) {
            QBluetoothUuid cu = uuidFromString(info.uuidStr);

            d->log(QString("  Char %1 props=0x%2").arg(cu.toString()).arg(info.props, 2, 16, QChar('0')));
            emit d->q->characteristicDiscovered(su, cu, info.props);
        }

        // NOTE: We do NOT auto-enable notifications here.
        // Each scale implementation should call enableNotifications() explicitly
        // at the appropriate time (like de1app does with "after 200 xxx_enable_weight_notifications").
        // Auto-enabling caused issues with Bookoo scale - enabling too early or double-enabling
        // can confuse some scales.

        d->cache();
        emit d->q->characteristicsDiscoveryFinished(su);
    }, Qt::QueuedConnection);
}

- (void)peripheral:(CBPeripheral *)peripheral
didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error
{
    Q_UNUSED(peripheral);

    auto* d = self.impl;
    if (!d) return;

    // Copy ALL ObjC data to Qt types NOW, before queuing
    QString uuidStr = nsToQs(characteristic.UUID.UUIDString);
    QString errorMsg = error ? nsToQs(error.localizedDescription) : QString();
    bool isNotifying = characteristic.isNotifying;

    QMetaObject::invokeMethod(d->q, [d, uuidStr, errorMsg, isNotifying]{
        if (!d->isValid) return;
        QBluetoothUuid cu = uuidFromString(uuidStr);

        if (!errorMsg.isEmpty()) {
            emit d->q->error(QString("Notify enable failed for %1: %2")
                             .arg(cu.toString(), errorMsg));
            return;
        }

        d->log(QString("Notifications enabled for %1 (isNotifying=%2)")
               .arg(cu.toString())
               .arg(isNotifying ? "true" : "false"));

        emit d->q->notificationsEnabled(cu);
    }, Qt::QueuedConnection);
}

- (void)peripheral:(CBPeripheral *)peripheral
didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error
{
    Q_UNUSED(peripheral);

    auto* d = self.impl;
    if (!d) return;

    if (error) return;

    // Copy ALL ObjC data to Qt types NOW, before queuing
    // (ObjC pointers become dangling when lambda executes later)
    NSData* data = characteristic.value;
    QByteArray bytes;
    if (data && data.length > 0)
        bytes = QByteArray((const char*)data.bytes, (int)data.length);

    QString uuidStr = nsToQs(characteristic.UUID.UUIDString);

    QMetaObject::invokeMethod(d->q, [d, uuidStr, bytes]{
        if (!d->isValid) return;  // Transport being destroyed
        QBluetoothUuid cu = uuidFromString(uuidStr);
        // Don't log every notification - too verbose at high rates (10/sec for Bookoo)
        emit d->q->characteristicChanged(cu, bytes);
    }, Qt::QueuedConnection);
}

- (void)peripheral:(CBPeripheral *)peripheral
didWriteValueForCharacteristic:(CBCharacteristic *)characteristic
             error:(NSError *)error
{
    Q_UNUSED(peripheral);

    auto* d = self.impl;
    if (!d) return;

    // Copy ALL ObjC data to Qt types NOW, before queuing
    QString uuidStr = nsToQs(characteristic.UUID.UUIDString);
    QString errorMsg = error ? nsToQs(error.localizedDescription) : QString();

    QMetaObject::invokeMethod(d->q, [d, uuidStr, errorMsg]{
        if (!d->isValid) return;
        QBluetoothUuid cu = uuidFromString(uuidStr);

        if (!errorMsg.isEmpty()) {
            d->log(QString("Write failed for %1: %2").arg(cu.toString(), errorMsg));
            emit d->q->error(QString("Write failed for %1: %2")
                             .arg(cu.toString(), errorMsg));
            return;
        }

        d->log(QString("Write success for %1").arg(cu.toString()));
        emit d->q->characteristicWritten(cu);
    }, Qt::QueuedConnection);
}

@end

#endif // Q_OS_IOS || Q_OS_MACOS

// ---------- C++ class ----------
CoreBluetoothScaleBleTransport::CoreBluetoothScaleBleTransport(QObject* parent)
    : ScaleBleTransport(parent)
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    d = new Impl;
    d->q = this;
    d->periph = nullptr;

    // alloc/init already returns +1 retain count, no need to retain again
    d->del = [[CBDelegateProxy alloc] init];
    d->del.impl = d;

    d->mgr = [[CBCentralManager alloc] initWithDelegate:d->del queue:dispatch_get_main_queue()];
#else
    d = nullptr;
#endif
}

CoreBluetoothScaleBleTransport::~CoreBluetoothScaleBleTransport() {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (d) {
        // Mark as invalid FIRST - this makes pending dispatch_async blocks no-op
        d->isValid = false;

        // Detach delegate so callbacks become no-ops
        if (d->del) {
            d->del.impl = nullptr;
        }

        // Disconnect cleanly (this handles main thread dispatch internally)
        disconnectFromDevice();

        // Release manager
        if (d->mgr) {
            d->mgr.delegate = nil;
            CB_RELEASE(d->mgr);
            d->mgr = nullptr;
        }

        // Release delegate
        if (d->del) {
            CB_RELEASE(d->del);
            d->del = nullptr;
        }

        delete d;
        d = nullptr;
    }
#endif
}

void CoreBluetoothScaleBleTransport::log(const QString& msg) {
    QString fullMsg = QString("[BLE CoreBluetooth] ") + msg;
    qDebug().noquote() << fullMsg;
    emit logMessage(fullMsg);
}

bool CoreBluetoothScaleBleTransport::isConnected() const {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    return d && d->connected;
#else
    return false;
#endif
}

void CoreBluetoothScaleBleTransport::connectToDevice(const QString& address, const QString& name) {
#if !defined(Q_OS_IOS) && !defined(Q_OS_MACOS)
    Q_UNUSED(address); Q_UNUSED(name);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#else
    if (!d) return;

    d->targetName = name;
    d->targetUuidString = address;
    d->targetUuidString.remove('{').remove('}');
    d->targetUuidString = d->targetUuidString.trimmed();

    log(QString("connectToDevice(name=%1 uuid=%2)").arg(d->targetName, d->targetUuidString));

    if (d->mgr.state != CBManagerStatePoweredOn) {
        log("Bluetooth not powered on yet; will retry on state update");
        return;
    }

    // Prefer retrieval by identifier if we have a UUID string
    if (!d->targetUuidString.isEmpty()) {
        NSUUID* nsuuid = [[NSUUID alloc] initWithUUIDString:qsToNs(d->targetUuidString)];
        if (nsuuid) {
            NSArray<CBPeripheral*>* arr = [d->mgr retrievePeripheralsWithIdentifiers:@[nsuuid]];
            if (arr.count > 0) {
                CBPeripheral* p = arr.firstObject;
                CB_RELEASE(d->periph);
                d->periph = CB_RETAIN(p);
                d->periph.delegate = d->del;
                log(QString("Connecting via retrievePeripheralsWithIdentifiers"));
                [d->mgr connectPeripheral:d->periph options:nil];
                return;
            }
        }
    }

    // Fallback: scan and match by name/uuid in didDiscover
    log("Starting scan for peripheral");
    [d->mgr scanForPeripheralsWithServices:nil
                                   options:@{ CBCentralManagerScanOptionAllowDuplicatesKey:@NO }];
#endif
}

void CoreBluetoothScaleBleTransport::connectToDevice(const QBluetoothDeviceInfo& device) {
#if !defined(Q_OS_IOS) && !defined(Q_OS_MACOS)
    Q_UNUSED(device);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#else
    QString uuid = device.deviceUuid().toString();
    QString name = device.name();
    connectToDevice(uuid, name);
#endif
}

void CoreBluetoothScaleBleTransport::disconnectFromDevice() {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!d) return;

    // CoreBluetooth calls must be on main thread
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            disconnectFromDevice();
        });
        return;
    }

    if (!d->mgr) return;

    if (d->mgr.isScanning) {
        [d->mgr stopScan];
    }

    if (d->periph) {
        log(QString("Disconnecting periph=%1").arg((quintptr)d->periph, 0, 16));
        [d->mgr cancelPeripheralConnection:d->periph];
        CB_RELEASE(d->periph);
        d->periph = nullptr;
    }

    d->connected = false;
    d->clearCaches();
#endif
}

void CoreBluetoothScaleBleTransport::discoverServices() {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!d || !d->isValid || !d->periph) { emit error("No peripheral"); return; }
    log("Discovering services");
    // On iOS, Qt main thread = dispatch main queue, so just call directly
    [d->periph discoverServices:nil];
#else
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::discoverCharacteristics(const QBluetoothUuid& serviceUuid) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!d || !d->isValid || !d->periph) { emit error("No peripheral"); return; }

    CBService* svc = d->findService(serviceUuid);
    if (svc) {
        log(QString("Discovering characteristics for %1").arg(serviceUuid.toString()));
        // On iOS, Qt main thread = dispatch main queue, so just call directly
        [d->periph discoverCharacteristics:nil forService:svc];
    } else if (!d->servicesDiscovered) {
        // Services not yet discovered - discover them first
        log(QString("Service %1 not cached, discovering all services").arg(serviceUuid.toString()));
        [d->periph discoverServices:nil];
    } else {
        // Services were discovered but this specific service wasn't found
        // This is normal - the device doesn't have this service
        log(QString("Service %1 not found on device").arg(serviceUuid.toString()));
    }
#else
    Q_UNUSED(serviceUuid);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::enableNotifications(const QBluetoothUuid& serviceUuid,
                                                        const QBluetoothUuid& characteristicUuid) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!d || !d->periph) { emit error("No peripheral"); return; }

    CBCharacteristic* ch = d->findChar(serviceUuid, characteristicUuid);
    if (!ch) {
        log(QString("Characteristic %1 not found for notifications").arg(characteristicUuid.toString()));
        emit error("Characteristic not found for notifications");
        return;
    }

    if (!(ch.properties & (CBCharacteristicPropertyNotify | CBCharacteristicPropertyIndicate))) {
        emit error("Characteristic does not support notify/indicate");
        return;
    }

    log(QString("Enabling notifications for %1").arg(characteristicUuid.toString()));
    // On iOS, Qt main thread = dispatch main queue, so just call directly
    [d->periph setNotifyValue:YES forCharacteristic:ch];
#else
    Q_UNUSED(serviceUuid); Q_UNUSED(characteristicUuid);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::writeCharacteristic(const QBluetoothUuid& serviceUuid,
                                                        const QBluetoothUuid& characteristicUuid,
                                                        const QByteArray& data,
                                                        WriteType writeType) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!d || !d->periph) { emit error("No peripheral"); return; }

    CBCharacteristic* ch = d->findChar(serviceUuid, characteristicUuid);
    if (!ch) {
        log(QString("Characteristic %1 not found for write").arg(characteristicUuid.toString()));
        emit error("Characteristic not found for write");
        return;
    }

    CBCharacteristicWriteType t = (writeType == WriteType::WithoutResponse)
        ? CBCharacteristicWriteWithoutResponse
        : CBCharacteristicWriteWithResponse;

    log(QString("Writing %1 bytes to %2").arg(data.size()).arg(characteristicUuid.toString()));

    // On iOS, Qt main thread = dispatch main queue, so just call directly
    NSData* ns = [NSData dataWithBytes:data.constData() length:data.size()];
    [d->periph writeValue:ns forCharacteristic:ch type:t];
#else
    Q_UNUSED(serviceUuid); Q_UNUSED(characteristicUuid); Q_UNUSED(data); Q_UNUSED(writeType);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::readCharacteristic(const QBluetoothUuid& serviceUuid,
                                                       const QBluetoothUuid& characteristicUuid) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!d || !d->periph) { emit error("No peripheral"); return; }

    CBCharacteristic* ch = d->findChar(serviceUuid, characteristicUuid);
    if (!ch) {
        log(QString("Characteristic %1 not found for read").arg(characteristicUuid.toString()));
        emit error("Characteristic not found for read");
        return;
    }

    log(QString("Reading characteristic %1").arg(characteristicUuid.toString()));
    // On iOS, Qt main thread = dispatch main queue, so just call directly
    [d->periph readValueForCharacteristic:ch];
#else
    Q_UNUSED(serviceUuid); Q_UNUSED(characteristicUuid);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}
