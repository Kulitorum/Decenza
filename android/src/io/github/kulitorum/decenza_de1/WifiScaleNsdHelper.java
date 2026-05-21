package io.github.kulitorum.decenza_de1;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import android.util.Log;

import org.qtproject.qt.android.QtNative;

import java.net.InetAddress;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Android helper for discovering the Half Decent Scale's WiFi endpoint.
 *
 * Why this exists: Android's stock resolver (getaddrinfo via QHostInfo) does
 * NOT do mDNS reliably for ".local" names — Qt's hostname lookup will return
 * NXDOMAIN. We use NsdManager (the Android-blessed NSD/mDNS API) to discover
 * _http._tcp services, match the one whose instance name matches the requested
 * hostname (e.g. "hds"), resolve it to an InetAddress, and hand the IP string
 * back to the Qt caller.
 *
 * The caller (WifiScaleDiscovery on Android) MUST run discoverHdsBlocking on
 * a non-UI thread — the method blocks on a CountDownLatch up to `timeoutMs`.
 *
 * Multicast lock: Android's WiFi driver filters multicast frames by default,
 * even with CHANGE_WIFI_MULTICAST_STATE declared in the manifest. We acquire
 * a WifiManager.MulticastLock for the duration of the discovery and release
 * it when finished. The lock is reference-counted, so nested acquires are
 * safe and only the matching release lowers the count.
 */
public class WifiScaleNsdHelper {
    private static final String TAG = "DecenzaWifiNsd";

    // openscale firmware advertises an _http._tcp service. We compare the
    // service instance name to the bare hostname stem (e.g. "hds" for
    // "hds.local") case-insensitively.
    private static final String SERVICE_TYPE = "_http._tcp.";

    // Multicast lock is a single shared instance keyed by class — Android's
    // setReferenceCounted(true) means nested probes do the right thing.
    private static WifiManager.MulticastLock sMulticastLock = null;

    /**
     * Acquire (or re-acquire) the WiFi multicast lock. Idempotent: the underlying
     * MulticastLock is reference-counted, so each acquire must be paired with
     * exactly one release. Returns true on success, false if the WifiManager is
     * unavailable (the caller can still try discovery — sometimes works, but
     * usually returns nothing).
     */
    public static synchronized boolean acquireMulticastLock() {
        try {
            Context ctx = QtNative.getContext();
            if (ctx == null) {
                Log.w(TAG, "acquireMulticastLock: no QtNative context");
                return false;
            }
            if (sMulticastLock == null) {
                WifiManager wm = (WifiManager) ctx.getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
                if (wm == null) {
                    Log.w(TAG, "acquireMulticastLock: WifiManager unavailable");
                    return false;
                }
                sMulticastLock = wm.createMulticastLock("DecenzaWifiScaleMdns");
                sMulticastLock.setReferenceCounted(true);
            }
            sMulticastLock.acquire();
            Log.d(TAG, "acquireMulticastLock: held");
            return true;
        } catch (Exception e) {
            Log.w(TAG, "acquireMulticastLock failed: " + e.getMessage());
            return false;
        }
    }

    /** Release one reference on the multicast lock. Safe to call if not held. */
    public static synchronized void releaseMulticastLock() {
        try {
            if (sMulticastLock != null && sMulticastLock.isHeld()) {
                sMulticastLock.release();
                Log.d(TAG, "releaseMulticastLock: released");
            }
        } catch (Exception e) {
            Log.w(TAG, "releaseMulticastLock failed: " + e.getMessage());
        }
    }

    /**
     * Blocking mDNS resolution for the Half Decent Scale.
     *
     * Discovers _http._tcp services on the local network, picks the first one
     * whose instance name matches `hostnameStem` (case-insensitive — strip
     * ".local" before passing), resolves it to an IP, and returns the IP as
     * a string. Returns empty string on timeout, no match, or any error.
     *
     * Caller must be off the Android main thread (this blocks).
     *
     * @param hostnameStem  bare hostname without ".local" (e.g. "hds")
     * @param timeoutMs     overall budget for discover + resolve
     * @return dotted-quad IP string, or "" on failure
     */
    public static String discoverHdsBlocking(final String hostnameStem, final int timeoutMs) {
        final Context ctx = QtNative.getContext();
        if (ctx == null) {
            Log.w(TAG, "discoverHdsBlocking: no QtNative context");
            return "";
        }

        final NsdManager nsd = (NsdManager) ctx.getApplicationContext()
            .getSystemService(Context.NSD_SERVICE);
        if (nsd == null) {
            Log.w(TAG, "discoverHdsBlocking: NsdManager unavailable");
            return "";
        }

        final boolean lockHeld = acquireMulticastLock();
        final CountDownLatch done = new CountDownLatch(1);
        final AtomicReference<String> result = new AtomicReference<>("");
        // Track outstanding resolves so we can short-circuit once we have one.
        final AtomicInteger resolvesInFlight = new AtomicInteger(0);
        final AtomicReference<NsdManager.DiscoveryListener> listenerRef = new AtomicReference<>();

        try {
            final NsdManager.DiscoveryListener discoveryListener = new NsdManager.DiscoveryListener() {
                @Override
                public void onDiscoveryStarted(String serviceType) {
                    Log.d(TAG, "onDiscoveryStarted: " + serviceType);
                }

                @Override
                public void onServiceFound(NsdServiceInfo serviceInfo) {
                    final String name = serviceInfo.getServiceName();
                    Log.d(TAG, "onServiceFound: " + name);
                    if (name == null) return;
                    // NsdManager mangles the instance name on Android — it may
                    // arrive percent-escaped (e.g. "hds\\032(2)") for collisions.
                    // Match prefix to be tolerant.
                    if (!name.toLowerCase().startsWith(hostnameStem.toLowerCase())) {
                        return;
                    }
                    resolvesInFlight.incrementAndGet();
                    nsd.resolveService(serviceInfo, new NsdManager.ResolveListener() {
                        @Override
                        public void onResolveFailed(NsdServiceInfo info, int errorCode) {
                            Log.d(TAG, "onResolveFailed: " + info.getServiceName()
                                + " errorCode=" + errorCode);
                            if (resolvesInFlight.decrementAndGet() == 0
                                && result.get().isEmpty()) {
                                // No outstanding resolves and still no result;
                                // discovery may still find more. Don't latch yet.
                            }
                        }

                        @Override
                        public void onServiceResolved(NsdServiceInfo info) {
                            final InetAddress host = info.getHost();
                            if (host == null) {
                                Log.d(TAG, "onServiceResolved: null host");
                                resolvesInFlight.decrementAndGet();
                                return;
                            }
                            final String ip = host.getHostAddress();
                            Log.d(TAG, "onServiceResolved: " + info.getServiceName()
                                + " -> " + ip);
                            // First winner takes it; latch the caller.
                            if (result.compareAndSet("", ip == null ? "" : ip)) {
                                done.countDown();
                            }
                            resolvesInFlight.decrementAndGet();
                        }
                    });
                }

                @Override public void onServiceLost(NsdServiceInfo serviceInfo) {}
                @Override public void onDiscoveryStopped(String serviceType) {}
                @Override
                public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                    Log.w(TAG, "onStartDiscoveryFailed: " + errorCode);
                    done.countDown();  // unblock caller — nothing will come
                }
                @Override
                public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                    Log.w(TAG, "onStopDiscoveryFailed: " + errorCode);
                }
            };
            listenerRef.set(discoveryListener);

            nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener);

            // Block up to timeoutMs for the first successful resolve.
            done.await(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (Exception e) {
            Log.w(TAG, "discoverHdsBlocking failed: " + e.getMessage());
        } finally {
            try {
                if (listenerRef.get() != null) {
                    nsd.stopServiceDiscovery(listenerRef.get());
                }
            } catch (Exception ignored) {}
            if (lockHeld) {
                releaseMulticastLock();
            }
        }

        final String ip = result.get();
        Log.d(TAG, "discoverHdsBlocking(" + hostnameStem + "): "
            + (ip.isEmpty() ? "no match" : ip));
        return ip;
    }
}
