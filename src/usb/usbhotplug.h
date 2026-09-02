#pragma once

#include <QObject>

class UsbScaleManager;
class USBManager;

// Which manager(s) a hotplug event belongs to. NOT either/or: the Half Decent
// Scale ships with a CH9102 (0x55D3), the same bridge chip the DE1 uses, so that
// id alone cannot tell them apart. Observed on hardware — an HDS attached as
// vid=0x1a86 pid=0x55d3 (serial HDS-900910), was routed to the DE1 manager, and
// timed out on the DE1's <+M> probe while never reaching the scale manager.
//
// So an ambiguous id notifies both, and the protocol probes decide: each manager's
// pass gives up on a device that does not answer its own handshake.
constexpr int kUsbScalePidCh340a = 0x7522;
constexpr int kUsbScalePidCh340b = 0x7523;
constexpr int kUsbSharedPidCh9102 = 0x55D3;   // DE1 *and* HDS

constexpr bool usbPidMayBeScale(int pid)
{
    return pid == kUsbScalePidCh340a || pid == kUsbScalePidCh340b
        || pid == kUsbSharedPidCh9102;
}

constexpr bool usbPidMayBeDe1(int pid)
{
    return pid == kUsbSharedPidCh9102;
}

/**
 * Android USB attach/detach, delivered to the two USB managers.
 *
 * Not gated by the USB scanning setting — see Settings::usbSerialEnabled.
 *
 * A no-op on every other platform: Qt provides no hotplug at all. QSerialPortInfo
 * is not a QObject, and qtserialport contains no udev_monitor,
 * IOServiceAddMatchingNotification or WM_DEVICECHANGE (checked in
 * ~/Qt/6.11.2/Src/qtserialport). Desktop detection remains the poll.
 */
class UsbHotplug : public QObject
{
    Q_OBJECT

public:
    // Either manager may be null; it simply never receives events.
    static void start(UsbScaleManager* scaleManager, USBManager* de1Manager);

    // Safe if start() never ran, and safe to call twice.
    static void stop();
};
