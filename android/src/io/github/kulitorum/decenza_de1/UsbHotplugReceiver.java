package io.github.kulitorum.decenza_de1;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.XmlResourceParser;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.util.Log;

import org.xmlpull.v1.XmlPullParser;

import java.util.ArrayList;
import java.util.List;

/**
 * Reports USB attach and detach to Qt, for every device the app supports.
 *
 * ONE receiver for both the DE1 and the scale. They are told apart by their
 * vendor/product ids and dispatched on the C++ side; a receiver per device kind
 * would duplicate the registration, the lifecycle and the id filter three ways.
 *
 * The manifest's USB_DEVICE_ATTACHED intent-filter is a different thing and stays
 * as it is: that one launches or foregrounds the app and grants permission when a
 * device is plugged in while the app is not already running. It is not delivered
 * to a running app as a signal, and it has no DETACHED counterpart — which is why
 * this exists.
 */
public class UsbHotplugReceiver extends BroadcastReceiver {

    private static final String TAG = "DecenzaUsbHotplug";

    /** Permission-result actions, owned here because this is what listens for them. */
    public static final String SCALE_PERMISSION_ACTION =
            "io.github.kulitorum.decenza_de1.USB_SCALE_PERMISSION";
    public static final String DE1_PERMISSION_ACTION =
            "io.github.kulitorum.decenza_de1.USB_PERMISSION";

    /** Reported to C++, which decides whether the ids are a DE1 or a scale. */
    static native void nativeOnUsbDeviceChanged(int vendorId, int productId, boolean attached);

    private static UsbHotplugReceiver sInstance;

    /** Supported (vendorId, productId) pairs, read once from res/xml/device_filter.xml. */
    private static List<int[]> sSupported;

    /**
     * Reads the supported ids from the SAME resource the manifest's intent-filter
     * uses, rather than restating them here. A second copy in Java would be free to
     * drift from the XML — and from the C++ constants that already mirror it —
     * with nothing failing when it did.
     */
    private static synchronized List<int[]> supportedDevices(Context context) {
        if (sSupported != null) {
            return sSupported;
        }
        List<int[]> out = new ArrayList<>();
        try (XmlResourceParser parser = context.getResources().getXml(R.xml.device_filter)) {
            int event = parser.getEventType();
            while (event != XmlPullParser.END_DOCUMENT) {
                if (event == XmlPullParser.START_TAG && "usb-device".equals(parser.getName())) {
                    int vid = parser.getAttributeIntValue(null, "vendor-id", -1);
                    int pid = parser.getAttributeIntValue(null, "product-id", -1);
                    if (vid >= 0 && pid >= 0) {
                        out.add(new int[] { vid, pid });
                    }
                }
                event = parser.next();
            }
            // Cached only on a COMPLETE parse. Assigning after the catch would cache
            // whatever the loop collected before throwing — with the current XML that
            // is the DE1 and not the scale, since the DE1 is listed first. "DE1 hotplug
            // works, scale hotplug never matches" is a far worse failure than retrying.
            sSupported = out;
        } catch (Exception e) {
            // Left uncached so the next event retries. Reported to C++ by register()'s
            // return value: android.util.Log goes to logcat only, and Decenza's own log
            // comes from a Qt message handler — so this line alone would leave a
            // user-submitted log with no trace of why hotplug matched nothing.
            Log.e(TAG, "Could not read device_filter.xml", e);
            return out;
        }
        return sSupported;
    }

    private static boolean isSupported(Context context, UsbDevice device) {
        for (int[] ids : supportedDevices(context)) {
            if (ids[0] == device.getVendorId() && ids[1] == device.getProductId()) {
                return true;
            }
        }
        return false;
    }

    /**
     * Registers the receiver and returns how many supported device ids were read
     * from device_filter.xml. Zero means hotplug will match nothing — the caller
     * reports that, because Java logging never reaches Decenza's own log.
     *
     * Idempotent: a second call re-reports the count rather than leaving two
     * registrations delivering every event twice.
     */
    public static synchronized int register(Context context) {
        final int supported = supportedDevices(context).size();
        if (sInstance != null) {
            return supported;
        }
        sInstance = new UsbHotplugReceiver();
        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        // The grant is otherwise seen only by a later hasDevice() poll, and with USB
        // scanning off there is no later poll.
        filter.addAction(SCALE_PERMISSION_ACTION);
        filter.addAction(DE1_PERMISSION_ACTION);
        // Not exported: these are system broadcasts, and RECEIVER_NOT_EXPORTED is
        // required from API 34 for a runtime-registered receiver.
        androidx.core.content.ContextCompat.registerReceiver(
                context.getApplicationContext(), sInstance, filter,
                androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED);
        Log.i(TAG, "USB hotplug receiver registered");
        return supported;
    }

    /** Unregisters the receiver. Idempotent, and safe if register() never ran. */
    public static synchronized void unregister(Context context) {
        if (sInstance == null) {
            return;
        }
        try {
            context.getApplicationContext().unregisterReceiver(sInstance);
        } catch (IllegalArgumentException e) {
            // Already gone (process teardown ordering). Not an error.
        }
        sInstance = null;
        Log.i(TAG, "USB hotplug receiver unregistered");
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        final String action = intent.getAction();
        if (action == null) {
            return;
        }
        final boolean isPermission = SCALE_PERMISSION_ACTION.equals(action)
                                  || DE1_PERMISSION_ACTION.equals(action);
        final boolean attached = UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action);
        if (!isPermission && !attached && !UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
            return;
        }
        UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
        if (device == null || !isSupported(context, device)) {
            return;
        }
        if (isPermission) {
            // A denial needs no report: the manager latches its request until the
            // device goes absent, so nothing retries.
            if (!intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                Log.i(TAG, "USB permission denied for " + device.getDeviceName());
                return;
            }
            // Reported as an attach: it re-runs the same probe pass, which now sees
            // the permission and connects.
            nativeOnUsbDeviceChanged(device.getVendorId(), device.getProductId(), true);
            return;
        }
        nativeOnUsbDeviceChanged(device.getVendorId(), device.getProductId(), attached);
    }
}
