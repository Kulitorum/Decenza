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

    // The Half Decent Scale (genuine Decent firmware — official thinner v2
    // scale, NOT a third-party openscale/ESP32 clone) advertises an _http._tcp
    // service. We compare the service instance name to the bare hostname stem
    // (e.g. "hds" for "hds.local") case-insensitively.
    private static final String SERVICE_TYPE = "_http._tcp.";

    // Multicast lock is a single shared instance keyed by class — Android's
    // setReferenceCounted(true) means nested probes do the right thing.
    private static WifiManager.MulticastLock sMulticastLock = null;

    // Tracks the in-flight discovery so subsequent probes (or a C++-driven
    // cancel) can tear it down BEFORE issuing a new discoverServices() call.
    // Android only permits one active DiscoveryListener per service type per
    // NsdManager instance; a second concurrent discoverServices() with the
    // same SERVICE_TYPE fires onStartDiscoveryFailed(FAILURE_ALREADY_ACTIVE)
    // on the new listener, which would silently break rapid re-scans.
    private static final Object sDiscoveryLock = new Object();
    private static volatile ActiveDiscovery sActiveDiscovery = null;

    private static final class ActiveDiscovery {
        final NsdManager nsd;
        final NsdManager.DiscoveryListener listener;
        final CountDownLatch done;
        ActiveDiscovery(NsdManager nsd, NsdManager.DiscoveryListener listener,
                        CountDownLatch done) {
            this.nsd = nsd;
            this.listener = listener;
            this.done = done;
        }
    }

    /**
     * Stop any in-flight NSD discovery started by this helper. Safe to call
     * when no discovery is active. Used by the C++ side's cancelInFlight()
     * (via JNI) to release the DiscoveryListener registration eagerly,
     * preventing FAILURE_ALREADY_ACTIVE on the next probe.
     */
    public static void cancelDiscovery() {
        final ActiveDiscovery active;
        synchronized (sDiscoveryLock) {
            active = sActiveDiscovery;
            sActiveDiscovery = null;
        }
        if (active == null) return;
        try {
            active.nsd.stopServiceDiscovery(active.listener);
            Log.d(TAG, "cancelDiscovery: stopped listener");
        } catch (Exception e) {
            // stopServiceDiscovery throws IllegalArgumentException if the
            // listener was never successfully registered (e.g. discoverServices
            // threw or onStartDiscoveryFailed already fired). Benign.
            Log.d(TAG, "cancelDiscovery: stop threw (benign): " + e.getMessage());
        }
        // Unblock the worker so it returns promptly.
        active.done.countDown();
    }

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

        // Any prior probe must surrender its DiscoveryListener before we
        // register a new one for SERVICE_TYPE, otherwise Android emits
        // FAILURE_ALREADY_ACTIVE on our discoverServices() call.
        cancelDiscovery();

        final boolean lockHeld = acquireMulticastLock();
        final CountDownLatch done = new CountDownLatch(1);
        final AtomicReference<String> result = new AtomicReference<>("");
        // Strict single-flight gate around resolveService(). API 28-33's
        // NsdManager only permits ONE outstanding resolve per NsdManager
        // instance — a concurrent call throws IllegalArgumentException
        // ("listener already in use") and kills the binder thread. The
        // three-arg Executor variant of resolveService that supports
        // concurrent resolves was added in API 34, which is above our
        // minSdk. Gating to one outstanding resolve at a time is the
        // correct fix; we re-open the gate on each callback so a later
        // onServiceFound match can still be resolved if the first one fails.
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
                    // Single-flight gate: skip if a resolve is already
                    // outstanding. The gate is re-opened on either callback
                    // below so another match can be retried.
                    if (!resolvesInFlight.compareAndSet(0, 1)) {
                        Log.d(TAG, "onServiceFound: resolve already in flight, deferring " + name);
                        return;
                    }
                    // Two-arg resolveService is deprecated in API 34 in favour
                    // of a three-arg Executor variant, but we still target the
                    // two-arg form for compatibility with minSdk 28.
                    @SuppressWarnings("deprecation")
                    final NsdManager.ResolveListener resolveListener = new NsdManager.ResolveListener() {
                        @Override
                        public void onResolveFailed(NsdServiceInfo info, int errorCode) {
                            Log.d(TAG, "onResolveFailed: " + info.getServiceName()
                                + " errorCode=" + errorCode);
                            resolvesInFlight.set(0);  // re-open gate for a retry
                        }

                        @Override
                        public void onServiceResolved(NsdServiceInfo info) {
                            final InetAddress host = info.getHost();
                            if (host == null) {
                                Log.d(TAG, "onServiceResolved: null host");
                                resolvesInFlight.set(0);
                                return;
                            }
                            final String ip = host.getHostAddress();
                            Log.d(TAG, "onServiceResolved: " + info.getServiceName()
                                + " -> " + ip);
                            // First non-empty winner takes it; latch the caller.
                            if (ip != null && !ip.isEmpty()
                                && result.compareAndSet("", ip)) {
                                done.countDown();
                            }
                            resolvesInFlight.set(0);
                        }
                    };
                    nsd.resolveService(serviceInfo, resolveListener);
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

            // Publish before discoverServices so a parallel cancelDiscovery()
            // call can find and stop us. The order matters: register first,
            // then start — never the reverse.
            synchronized (sDiscoveryLock) {
                sActiveDiscovery = new ActiveDiscovery(nsd, discoveryListener, done);
            }
            nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener);

            // Block up to timeoutMs for the first successful resolve.
            done.await(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (Exception e) {
            Log.w(TAG, "discoverHdsBlocking failed: " + e.getMessage());
        } finally {
            // Clear our slot from the active-discovery tracker, but only if
            // it's still pointing at OUR listener — a concurrent probe may
            // have replaced it via cancelDiscovery + new ActiveDiscovery.
            synchronized (sDiscoveryLock) {
                if (sActiveDiscovery != null
                    && sActiveDiscovery.listener == listenerRef.get()) {
                    sActiveDiscovery = null;
                }
            }
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
