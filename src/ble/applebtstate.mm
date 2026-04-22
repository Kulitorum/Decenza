#include "applebtstate.h"

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)

#import <CoreBluetooth/CoreBluetooth.h>
#include <QDebug>

@interface DecenzaBtStateObserver : NSObject <CBCentralManagerDelegate>
@property(nonatomic, strong) CBCentralManager *manager;
@property(nonatomic, assign) AppleBtState *owner;
- (BOOL)isUnavailable;
@end

@implementation DecenzaBtStateObserver

- (instancetype)initWithOwner:(AppleBtState *)owner {
    if ((self = [super init])) {
        _owner = owner;
        // ShowPowerAlertKey=NO keeps CoreBluetooth from surfacing its own
        // system alert when BT is off — we render our own in-app banner.
        _manager = [[CBCentralManager alloc]
                        initWithDelegate:self
                                   queue:dispatch_get_main_queue()
                                 options:@{CBCentralManagerOptionShowPowerAlertKey: @NO}];
    }
    return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
    Q_UNUSED(central);
    if (_owner) {
        emit _owner->stateChanged();
    }
}

- (BOOL)isUnavailable {
    switch (_manager.state) {
        case CBManagerStatePoweredOff:
        case CBManagerStateUnauthorized:
        case CBManagerStateUnsupported:
            return YES;
        case CBManagerStateUnknown:
        case CBManagerStateResetting:
        case CBManagerStatePoweredOn:
        default:
            return NO;
    }
}

@end

// This TU is compiled in MRC (non-ARC) mode, so __bridge_* casts would be
// no-ops and emit warnings. We use plain casts and explicit release/retain.

AppleBtState::AppleBtState(QObject* parent) : QObject(parent) {
    m_observer = (void*)[[DecenzaBtStateObserver alloc] initWithOwner:this];
}

AppleBtState::~AppleBtState() {
    if (m_observer) {
        DecenzaBtStateObserver* obs = (DecenzaBtStateObserver*)m_observer;
        obs.owner = nullptr;
        obs.manager.delegate = nil;
        [obs release];
        m_observer = nullptr;
    }
}

bool AppleBtState::isUnavailable() const {
    if (!m_observer) return false;
    DecenzaBtStateObserver* obs = (DecenzaBtStateObserver*)m_observer;
    return [obs isUnavailable];
}

#endif  // Apple targets only — Windows/Linux don't compile this TU.
