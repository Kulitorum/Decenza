#include "corebluetoothscalebletransport.h"

#include "ble/scales/scalelogging.h"

#include <QDebug>
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
    // A connect has been handed to CoreBluetooth and has not resolved. Distinct
    // from `connected`: between connectPeripheral: and didConnectPeripheral:
    // both are false, yet the platform is holding the connection slot — and
    // CoreBluetooth puts NO timeout on that wait, so it can be held
    // indefinitely. See isConnecting().
    bool pendingConnect = false;
    bool servicesDiscovered = false;  // Prevent re-discovery loops
    // The characteristic a queued READ is outstanding for, so a notification on
    // it can be told from its response. Null when the in-flight operation is not
    // a read.
    QBluetoothUuid readKeyInFlight;
    bool isValid = true;  // Set to false when transport is being destroyed

    QString targetName;
    QString targetUuidString;

    QHash<QBluetoothUuid, CBService*> services;
    QHash<QString, CBCharacteristic*> chars;
    QSet<QBluetoothUuid> charsDiscoveredForService;  // Track which services have had chars discovered

    void log(const QString& m) { if (q && isValid) q->log(m); }

    // Shared-GATT-queue forwarders. The delegate is an Objective-C class and so
    // cannot reach ScaleBleTransport's protected members; Impl is a nested
    // struct of the transport and can. One line each, no logic — a place to
    // stand, not a layer.
    // True when the slot holds a READ for this characteristic. CoreBluetooth
    // cannot tell a read response from a notification, so the transport records
    // which kind it asked for.
    bool readInFlight(const QBluetoothUuid& key) const {
        return q && isValid && q->heldGattKey() == key && readKeyInFlight == key;
    }
    void queueSucceeded() { readKeyInFlight = QBluetoothUuid(); if (q && isValid) q->completeGattOperation(); }
    void queueSucceeded(const QBluetoothUuid& key) { readKeyInFlight = QBluetoothUuid(); if (q && isValid) q->completeGattOperation(key); }
    void queueFailed() { readKeyInFlight = QBluetoothUuid(); if (q && isValid) q->failGattOperation(); }
    void queueReleased() { readKeyInFlight = QBluetoothUuid(); if (q && isValid) q->releaseGattQueue(); }

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

    // A GATT operation is safe only when the adapter is powered on AND the
    // peripheral is actually connected. Issuing a write/read to a stale
    // CBPeripheral during a CBManagerStateResetting/PoweredOff transition
    // crashes inside CoreBluetooth (issue #1405: the scale heartbeat timer kept
    // firing through an adapter reset). CB invalidates all peripherals on those
    // states and may NOT deliver didDisconnectPeripheral for an adapter-state
    // change, so our `connected` flag can stay stale indefinitely — guard on the
    // live CB state here instead.
    bool readyForIO() const {
        return isValid && mgr && periph
            && mgr.state == CBManagerStatePoweredOn
            && periph.state == CBPeripheralStateConnected;
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

        if (state == CBManagerStatePoweredOn) {
            if ((!d->targetName.isEmpty() || !d->targetUuidString.isEmpty()) && !d->periph) {
                d->log("PoweredOn: attempting connect now");
                d->q->connectToDevice(d->targetUuidString, d->targetName);
            }
        } else if (d->connected &&
                   (state == CBManagerStatePoweredOff ||
                    state == CBManagerStateUnsupported ||
                    state == CBManagerStateUnauthorized)) {
            // Terminal adapter loss while connected: CB has invalidated the
            // peripheral and (per Apple) does NOT deliver didDisconnectPeripheral
            // for an adapter-state change, so synthesize the disconnect —
            // otherwise our `connected` flag stays stale and the scale never
            // reconnects after Bluetooth is toggled back on.
            //
            // We deliberately do NOT do this for the transient states
            // (CBManagerStateResetting/Unknown — "an update is imminent", usually
            // self-recovers): tearing the link down there would bounce the scale
            // mid-shot, the exact disruption #1176 forbids. The readyForIO() guard
            // already blocks writes during those states, and that — not this
            // synthesized disconnect — is what prevents the #1405 crash. If a
            // Resetting does escalate to PoweredOff, this branch fires on that
            // follow-up state.
            d->connected = false;
            d->clearCaches();
            d->log(QString("Central state=%1 (terminal) — treating as disconnect").arg(state));
            emit d->q->disconnected();
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

    d->pendingConnect = true;
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
        d->pendingConnect = false;
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
        d->pendingConnect = false;
        d->connected = false;
        d->clearCaches();
        // A dead link must not hold the radio for every other device, and this
        // transport's queued work must not reach the next connection.
        d->queueReleased();
        d->log(QString("Disconnected: %1").arg(reason));
        emit d->q->disconnected();
    }, Qt::QueuedConnection);
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
    auto* d = self.impl;
    if (!d) return;

    // Ignore if we already processed services (prevents duplicate handling).
    // Still terminal for the operation: a queued discovery that this guard
    // swallows would otherwise hold the shared radio for the full discovery
    // timeout. Harmless dedupe before the queue existed; a 20 s process-wide
    // stall after it.
    if (d->servicesDiscovered) {
        QMetaObject::invokeMethod(d->q, [d]{
            if (d->isValid) d->queueSucceeded();
        }, Qt::QueuedConnection);
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

    // Characteristic discovery used to be kicked off for every service right
    // here, ahead of the Qt signals, to overlap it with notifying Qt. It is not
    // any more: the drivers are shared with the Qt transport and they all call
    // discoverCharacteristics() from servicesDiscoveryFinished(), so this was a
    // platform-only head start that also put discovery on the radio outside the
    // shared queue — the one operation #1819 shows colliding with the DE1.
    // Ordering now matches the Qt transport exactly.

    // Now notify Qt thread
    QMetaObject::invokeMethod(d->q, [d, errorMsg, serviceUuids]{
        if (!d->isValid) return;
        if (!errorMsg.isEmpty()) {
            d->queueFailed();
            emit d->q->error(QString("Service discovery error: %1").arg(errorMsg));
            return;
        }

        // Ends the service-discovery operation.
        d->queueSucceeded();

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

    // Ignore if we already processed characteristics for this service (prevents
    // duplicates). Terminal for the operation, for the same reason as the
    // services guard above.
    if (d->charsDiscoveredForService.contains(serviceUuid)) {
        QMetaObject::invokeMethod(d->q, [d, serviceUuid]{
            if (d->isValid) d->queueSucceeded(serviceUuid);
        }, Qt::QueuedConnection);
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
        QBluetoothUuid su = uuidFromString(serviceUuidStr);
        if (!errorMsg.isEmpty()) {
            d->queueFailed();
            emit d->q->error(QString("Char discovery error: %1").arg(errorMsg));
            return;
        }

        // Ends the characteristic-discovery operation for this service.
        d->queueSucceeded(su);

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
            d->queueFailed();
            emit d->q->error(QString("Notify enable failed for %1: %2")
                             .arg(cu.toString(), errorMsg));
            return;
        }

        d->queueSucceeded(cu);

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

    if (error) {
        // Was a bare `return`: no log at any tier, no terminal outcome, no
        // error() to the driver. Under the shared queue that also held the radio
        // for every device until the operation clock expired. Every sibling
        // delegate below reports and releases; this one was missed, and it is
        // the same defect shape as #1819 one file over.
        QString errorMsg = nsToQs(error.localizedDescription);
        QString uuidStr = nsToQs(characteristic.UUID.UUIDString);
        QMetaObject::invokeMethod(d->q, [d, uuidStr, errorMsg]{
            if (!d->isValid) return;
            d->queueFailed();
            emit d->q->error(QString("Read failed for %1: %2")
                                 .arg(uuidFromString(uuidStr).toString(), errorMsg));
        }, Qt::QueuedConnection);
        return;
    }

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
        // CoreBluetooth delivers a read RESPONSE and an unsolicited notification
        // through this one callback with nothing to tell them apart, so this can
        // only release a READ. Scoped to reads deliberately: it used to release
        // whatever was in flight on this characteristic, which for a write meant
        // a notification arriving mid-write freed the slot while the write was
        // still on the wire — the exact concurrency this queue exists to prevent.
        // A read released early is harmless (the response still arrives); a write
        // released early is not. The platform does not carry the distinction, so
        // the transport keeps it.
        if (d->readInFlight(cu)) d->queueSucceeded(cu);
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
            d->queueFailed();
            d->log(QString("Write failed for %1: %2").arg(cu.toString(), errorMsg));
            emit d->q->error(QString("Write failed for %1: %2")
                             .arg(cu.toString(), errorMsg));
            return;
        }

        // Write success — don't log (heartbeat writes flood the log at 1/sec)
        d->queueSucceeded(cu);
        emit d->q->characteristicWritten(cu);
    }, Qt::QueuedConnection);
}

@end

#endif // Q_OS_IOS || Q_OS_MACOS

// ---------- C++ class ----------
CoreBluetoothScaleBleTransport::CoreBluetoothScaleBleTransport(QObject* parent,
                                                               BleGattQueue* queue)
    : ScaleBleTransport(parent, queue)
{
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    m_impl = new Impl;
    m_impl->q = this;
    m_impl->periph = nullptr;

    // alloc/init already returns +1 retain count, no need to retain again
    m_impl->del = [[CBDelegateProxy alloc] init];
    m_impl->del.impl = m_impl;

    m_impl->mgr = [[CBCentralManager alloc] initWithDelegate:m_impl->del queue:dispatch_get_main_queue()];
#else
    m_impl = nullptr;
#endif
}

CoreBluetoothScaleBleTransport::~CoreBluetoothScaleBleTransport() {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (m_impl) {
        // Mark as invalid FIRST - this makes pending dispatch_async blocks no-op
        m_impl->isValid = false;

        // Detach delegate so callbacks become no-ops
        if (m_impl->del) {
            m_impl->del.impl = nullptr;
        }

        // Disconnect cleanly (this handles main thread dispatch internally)
        disconnectFromDevice();

        // Release manager
        if (m_impl->mgr) {
            m_impl->mgr.delegate = nil;
            CB_RELEASE(m_impl->mgr);
            m_impl->mgr = nullptr;
        }

        // Release delegate
        if (m_impl->del) {
            CB_RELEASE(m_impl->del);
            m_impl->del = nullptr;
        }

        delete m_impl;
        m_impl = nullptr;
    }
#endif
}

void CoreBluetoothScaleBleTransport::log(const QString& msg) {
    SCALE_LOG_TAGGED("BLE CoreBluetooth", msg);
}

bool CoreBluetoothScaleBleTransport::isConnected() const {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    return m_impl && m_impl->connected;
#else
    return false;
#endif
}

bool CoreBluetoothScaleBleTransport::isConnecting() const {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    // Implemented rather than left on the base class's `return false`. The
    // scale connection-timeout teardown gates on isConnected() || isConnecting()
    // (blemanager.cpp), so inheriting the default would silently opt Apple
    // platforms out of that fix — and the case is WORSE here than on Android:
    // connectPeripheral: has no timeout of its own, so an unresolved attempt is
    // held indefinitely rather than for ~30 s.
    //
    // There is no didFailToConnectPeripheral: handler on this delegate, so a
    // connect that fails without ever disconnecting leaves this true until the
    // next disconnectFromDevice(). That is the safe direction: it makes the
    // timeout teardown fire, which issues cancelPeripheralConnection and clears
    // the flag. It is not a substitute for handling that callback.
    return m_impl && m_impl->pendingConnect;
#else
    return false;
#endif
}

void CoreBluetoothScaleBleTransport::connectToDevice(const QString& address, const QString& name) {
#if !defined(Q_OS_IOS) && !defined(Q_OS_MACOS)
    Q_UNUSED(address); Q_UNUSED(name);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#else
    if (!m_impl) return;

    m_impl->targetName = name;
    m_impl->targetUuidString = address;
    m_impl->targetUuidString.remove('{').remove('}');
    m_impl->targetUuidString = m_impl->targetUuidString.trimmed();

    log(QString("connectToDevice(name=%1 uuid=%2)").arg(m_impl->targetName, m_impl->targetUuidString));

    if (m_impl->mgr.state != CBManagerStatePoweredOn) {
        log("Bluetooth not powered on yet; will retry on state update");
        return;
    }

    // Prefer retrieval by identifier if we have a UUID string
    if (!m_impl->targetUuidString.isEmpty()) {
        NSUUID* nsuuid = [[NSUUID alloc] initWithUUIDString:qsToNs(m_impl->targetUuidString)];
        if (nsuuid) {
            NSArray<CBPeripheral*>* arr = [m_impl->mgr retrievePeripheralsWithIdentifiers:@[nsuuid]];
            if (arr.count > 0) {
                CBPeripheral* p = arr.firstObject;
                CB_RELEASE(m_impl->periph);
                m_impl->periph = CB_RETAIN(p);
                m_impl->periph.delegate = m_impl->del;
                log(QString("Connecting via retrievePeripheralsWithIdentifiers"));
                m_impl->pendingConnect = true;
                [m_impl->mgr connectPeripheral:m_impl->periph options:nil];
                return;
            }
        }
    }

    // Fallback: scan and match by name/uuid in didDiscover
    log("Starting scan for peripheral");
    [m_impl->mgr scanForPeripheralsWithServices:nil
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
    if (!m_impl) return;

    // CoreBluetooth calls must be on main thread
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            disconnectFromDevice();
        });
        return;
    }

    if (!m_impl->mgr) return;

    if (m_impl->mgr.isScanning) {
        [m_impl->mgr stopScan];
    }

    if (m_impl->periph) {
        log(QString("Disconnecting periph=%1").arg((quintptr)m_impl->periph, 0, 16));
        [m_impl->mgr cancelPeripheralConnection:m_impl->periph];
        CB_RELEASE(m_impl->periph);
        m_impl->periph = nullptr;
    }

    m_impl->pendingConnect = false;
    m_impl->connected = false;
    m_impl->clearCaches();
    // Before the peripheral goes: release the slot and drop our queued
    // operations, so nothing is issued against a released CBPeripheral and no
    // other device waits on work that will never run.
    releaseGattQueue();
#endif
}

void CoreBluetoothScaleBleTransport::discoverServices() {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    // Null key: service discovery targets no characteristic, and its completion
    // names none either. Same shape as the Qt transport.
    submitGattOperation(QBluetoothUuid(), QStringLiteral("scale discover services"), [this]() {
        if (!m_impl || !m_impl->isValid || !m_impl->periph) {
            emit error("No peripheral");
            failGattOperation();
            return;
        }
        log("Discovering services");
        // On iOS, Qt main thread = dispatch main queue, so just call directly
        [m_impl->periph discoverServices:nil];
    }, BleGatt::DISCOVERY_TIMEOUT_MS);
#else
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::discoverCharacteristics(const QBluetoothUuid& serviceUuid) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    if (!m_impl || !m_impl->isValid || !m_impl->periph) { emit error("No peripheral"); return; }

    submitGattOperation(serviceUuid, QStringLiteral("scale discover characteristics"),
                        [this, serviceUuid]() {
        if (!m_impl || !m_impl->isValid || !m_impl->periph) {
            emit error("No peripheral");
            failGattOperation();
            return;
        }
        CBService* svc = m_impl->findService(serviceUuid);
        if (svc) {
            log(QString("Discovering characteristics for %1").arg(serviceUuid.toString()));
            // On iOS, Qt main thread = dispatch main queue, so just call directly
            [m_impl->periph discoverCharacteristics:nil forService:svc];
        } else if (!m_impl->servicesDiscovered) {
            // Services not yet discovered - discover them first. This completes
            // through didDiscoverServices, which releases the slot keylessly, so
            // the operation still ends.
            log(QString("Service %1 not cached, discovering all services").arg(serviceUuid.toString()));
            [m_impl->periph discoverServices:nil];
        } else {
            // Services were discovered but this specific service wasn't found.
            // This is normal - the device doesn't have this service. Nothing was
            // issued, so nothing will complete it.
            log(QString("Service %1 not found on device").arg(serviceUuid.toString()));
            failGattOperation();
        }
    }, BleGatt::DISCOVERY_TIMEOUT_MS);
#else
    Q_UNUSED(serviceUuid);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::enableNotifications(const QBluetoothUuid& serviceUuid,
                                                        const QBluetoothUuid& characteristicUuid) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    submitGattOperation(characteristicUuid, QStringLiteral("scale enable notifications"),
                        [this, serviceUuid, characteristicUuid]() {
        if (!m_impl || !m_impl->periph) {
            emit error("No peripheral");
            failGattOperation();
            return;
        }
        if (!m_impl->readyForIO()) {
            log("Skipping notify-enable — link not ready (adapter/peripheral not connected)");
            failGattOperation();
            return;
        }

        CBCharacteristic* ch = m_impl->findChar(serviceUuid, characteristicUuid);
        if (!ch) {
            log(QString("Characteristic %1 not found for notifications").arg(characteristicUuid.toString()));
            emit error("Characteristic not found for notifications");
            failGattOperation();
            return;
        }

        if (!(ch.properties & (CBCharacteristicPropertyNotify | CBCharacteristicPropertyIndicate))) {
            emit error("Characteristic does not support notify/indicate");
            failGattOperation();
            return;
        }

        log(QString("Enabling notifications for %1").arg(characteristicUuid.toString()));
        // On iOS, Qt main thread = dispatch main queue, so just call directly
        [m_impl->periph setNotifyValue:YES forCharacteristic:ch];
    });
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
    submitGattOperation(characteristicUuid, QStringLiteral("scale write"),
                        [this, serviceUuid, characteristicUuid, data, writeType]() {
        if (!m_impl || !m_impl->periph) {
            emit error("No peripheral");
            failGattOperation();
            return;
        }
        if (!m_impl->readyForIO()) {
            // Adapter resetting/off or peripheral no longer connected — writing to a
            // stale CBPeripheral crashes CoreBluetooth (#1405). Drop the write; a real
            // peripheral drop is torn down by didDisconnectPeripheral, and a terminal
            // adapter loss by the centralManagerDidUpdateState handler.
            log("Skipping write — link not ready (adapter/peripheral not connected)");
            failGattOperation();
            return;
        }

        CBCharacteristic* ch = m_impl->findChar(serviceUuid, characteristicUuid);
        if (!ch) {
            log(QString("Characteristic %1 not found for write").arg(characteristicUuid.toString()));
            emit error("Characteristic not found for write");
            failGattOperation();
            return;
        }

        const bool withoutResponse = (writeType == WriteType::WithoutResponse);

        // Don't log routine writes — failures are logged in the callback

        // On iOS, Qt main thread = dispatch main queue, so just call directly
        NSData* ns = [NSData dataWithBytes:data.constData() length:data.size()];
        [m_impl->periph writeValue:ns forCharacteristic:ch
                              type:withoutResponse ? CBCharacteristicWriteWithoutResponse
                                                   : CBCharacteristicWriteWithResponse];

        if (withoutResponse) {
            // Complete at issue: CoreBluetooth calls didWriteValueForCharacteristic
            // only for writes WITH response. Holding the slot would mean holding
            // it until the operation clock expired, every heartbeat.
            completeGattOperation(characteristicUuid);
        }
    });
#else
    Q_UNUSED(serviceUuid); Q_UNUSED(characteristicUuid); Q_UNUSED(data); Q_UNUSED(writeType);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}

void CoreBluetoothScaleBleTransport::readCharacteristic(const QBluetoothUuid& serviceUuid,
                                                       const QBluetoothUuid& characteristicUuid) {
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    submitGattOperation(characteristicUuid, QStringLiteral("scale read"),
                        [this, serviceUuid, characteristicUuid]() {
        if (!m_impl || !m_impl->periph) {
            emit error("No peripheral");
            failGattOperation();
            return;
        }
        if (!m_impl->readyForIO()) {
            log("Skipping read — link not ready (adapter/peripheral not connected)");
            failGattOperation();
            return;
        }

        CBCharacteristic* ch = m_impl->findChar(serviceUuid, characteristicUuid);
        if (!ch) {
            log(QString("Characteristic %1 not found for read").arg(characteristicUuid.toString()));
            emit error("Characteristic not found for read");
            failGattOperation();
            return;
        }

        log(QString("Reading characteristic %1").arg(characteristicUuid.toString()));
        m_impl->readKeyInFlight = characteristicUuid;
        // On iOS, Qt main thread = dispatch main queue, so just call directly
        [m_impl->periph readValueForCharacteristic:ch];
    });
#else
    Q_UNUSED(serviceUuid); Q_UNUSED(characteristicUuid);
    emit error("CoreBluetoothScaleBleTransport is only available on iOS");
#endif
}
