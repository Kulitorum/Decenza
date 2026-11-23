#pragma once

#include <QObject>

// Apple-platform adapter for CoreBluetooth's CBCentralManager.state.
//
// Backs BLEManager::isBluetoothAvailable() on both Apple platforms. iOS has no
// QBluetoothLocalDevice, so CBCentralManager is the only source there, and using
// it on macOS too gives one path that also reports a denied Bluetooth permission.
// (Qt 6.12's macOS hostMode() does read the controller's power state,
// qtconnectivity/src/bluetooth/qbluetoothlocaldevice_macos.mm:435-447.) Its delegate fires whenever the state
// transitions (e.g. the user toggles BT, revokes permission, or airplane mode flips).
//
// This header is a plain QObject declaration and compiles on every
// platform. The implementation in applebtstate.mm is Apple-only and is
// only wired into the build on APPLE targets (see CMakeLists.txt); the
// forward declaration in blemanager.h is correspondingly guarded so
// non-Apple translation units don't reference a symbol that will never
// link.
class AppleBtState : public QObject {
    Q_OBJECT
public:
    explicit AppleBtState(QObject* parent = nullptr);
    ~AppleBtState() override;

    // True when CoreBluetooth has reported PoweredOff, Unauthorized, or
    // Unsupported. False in the Unknown / Resetting / PoweredOn states —
    // the initial Unknown window (before the delegate first fires) keeps
    // the banner hidden rather than flashing it on every app launch.
    bool isUnavailable() const;

signals:
    void stateChanged();

private:
    void* m_observer = nullptr;  // DecenzaBtStateObserver* (Obj-C, type-erased)
};
