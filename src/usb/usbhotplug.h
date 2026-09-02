#pragma once

#include <QObject>

class UsbScaleManager;
class USBManager;

// Which manager a hotplug event belongs to, decided from the product id alone.
//
// Free and cross-platform on purpose: it is the one part of hotplug that is pure
// logic, and it carries a real drift risk — device_filter.xml is the single list
// of supported ids, and a scale id added there but not here would route that
// scale's events to the DE1 manager, silently. tst_usbdecentscale.cpp's
// deviceFilterIdsRouteToTheRightManager binds this list to that XML.
enum class UsbDeviceKind { Scale, De1 };

// Scale product ids (WCH CH340 variants) — the single C++ declaration;
// UsbScaleManager::isScalePid() defers to the classifier below. Everything else
// supported is the DE1, so adding a scale id is the only edit a new scale needs.
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
 * ONE receiver serves both device kinds. `android/res/xml/device_filter.xml`
 * already lists the DE1's CH9102 alongside both scale CH340 ids, the Java side
 * filters against that same resource, and this class decides which manager an
 * event belongs to. A receiver per manager would duplicate the registration, the
 * lifecycle handling and the id filter, which is the copy-per-caller drift the
 * centralize rule exists to stop.
 *
 * NOT gated by the USB scanning setting. That setting exists to stop periodic
 * scanning, which has a recurring cost; a registered receiver that receives
 * nothing has none. So a plugged-in device works on Android with scanning off.
 *
 * A no-op on every other platform: Qt provides no hotplug of any kind
 * (`QSerialPortInfo` is not even a `QObject`, and `qtserialport` contains no
 * `udev_monitor`, `IOServiceAddMatchingNotification` or `WM_DEVICECHANGE`), so
 * desktop detection remains the poll.
 */
class UsbHotplug : public QObject
{
    Q_OBJECT

public:
    // Both managers are optional; a null one simply never receives events.
    // Registers the Java receiver on Android, does nothing elsewhere.
    static void start(UsbScaleManager* scaleManager, USBManager* de1Manager);

    // Releases the Java receiver. Safe if start() was never called, and safe to
    // call twice — Android requires the unregister, and a receiver left behind
    // outlives the objects its events would reach.
    static void stop();
};
