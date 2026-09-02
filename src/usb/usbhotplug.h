#pragma once

#include <QObject>

class UsbScaleManager;
class USBManager;

// Anything not a scale id is assumed to be the DE1, so a scale id added to
// device_filter.xml but not here is silently routed to the wrong manager.
// tst_usbdecentscale.cpp's deviceFilterIdsRouteToTheRightManager binds the two.
enum class UsbDeviceKind { Scale, De1 };

constexpr int kUsbScalePid1 = 0x7522;
constexpr int kUsbScalePid2 = 0x7523;

constexpr UsbDeviceKind usbDeviceKindForPid(int productId)
{
    return (productId == kUsbScalePid1 || productId == kUsbScalePid2)
               ? UsbDeviceKind::Scale
               : UsbDeviceKind::De1;
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
