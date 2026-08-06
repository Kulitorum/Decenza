package io.github.kulitorum.decenza_de1;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.util.Log;

import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Browses for Half Decent Scales with Android's own DNS-SD daemon (NsdManager),
 * as a SECOND path beside the app's mjansson browse — not a replacement.
 *
 * Why a second path, when the app already browses the same service: the two fail
 * for different reasons, and the one that fails is the one we ship.
 *
 * The app's browse queries from an EPHEMERAL source port. Under RFC 6762 section
 * 6.7 that is a "legacy" query, which a responder must answer by UNICAST — and to
 * unicast, the scale's lwIP has to resolve the querier's IP to a MAC. ESP-IDF
 * ships ETHARP_TRUST_IP_MAC off, so the scale does not learn the querier's MAC
 * from the multicast query frame it just parsed; it must ARP, and it queues only
 * one packet while that ARP is outstanding. A Wi-Fi power-saving Android tablet is
 * exactly the client that answers that ARP late or not at all, so the reply is
 * dropped and the query looks unanswered.
 *
 * Measured on 2026-08-06, one tablet, one LAN, two scales on firmware 3.1.13:
 * the tablet got 0 records from 7 A-record queries over 5 s, repeatedly for hours,
 * while a Mac resolved the same name in 272 ms. Making the tablet open one TCP
 * connection to the scale (which makes the TABLET ARP, teaching the scale its MAC)
 * flipped it to 1 query / 272 ms — and only for the scale it had contacted; the
 * second scale on the same LAN stayed silent to that tablet while answering the
 * Mac normally.
 *
 * NsdManager does not have this problem. The system daemon owns port 5353, so its
 * queries are not "legacy" and responses come back MULTICAST to the group — no
 * ARP, no per-peer state on the responder. It also sees the scale's unsolicited
 * announcements, which need no query at all. That makes it the path that can find
 * a scale this device has never talked to, which is precisely the case the app's
 * own browse cannot recover on its own.
 *
 * This class replaces an earlier NsdManager helper deleted in #1249 for browsing
 * "_http._tcp" — the wrong service type, because the scale published only a
 * hostname at the time. openscale v3.0.9 added _decentscale._tcp (which is what
 * the app's own browse uses), so the reason that helper was removed no longer
 * holds.
 *
 * browseBlocking() blocks up to timeoutMs and MUST be called off the UI thread.
 * The multicast lock ShotServer holds for the app lifetime covers reception.
 */
public class WifiScaleNsdHelper {
    private static final String TAG = "DecenzaWifiNsd";

    // Must match kServiceType on the C++ side. NsdManager wants the trailing dot.
    private static final String SERVICE_TYPE = "_decentscale._tcp.";

    // Android permits one active DiscoveryListener per service type per NsdManager,
    // and a second concurrent discoverServices() for the same type fails the NEW
    // listener with FAILURE_ALREADY_ACTIVE. Tearing the previous one down first is
    // what keeps rapid re-scans working.
    private static final Object sDiscoveryLock = new Object();
    private static volatile Active sActive = null;

    private static final class Active {
        final NsdManager nsd;
        final NsdManager.DiscoveryListener listener;
        Active(NsdManager nsd, NsdManager.DiscoveryListener listener) {
            this.nsd = nsd;
            this.listener = listener;
        }
    }

    /** Stops any in-flight browse. Safe to call when none is running. */
    public static void cancelBrowse() {
        synchronized (sDiscoveryLock) {
            final Active active = sActive;
            sActive = null;
            if (active == null) return;
            try {
                active.nsd.stopServiceDiscovery(active.listener);
            } catch (IllegalArgumentException e) {
                // Listener already unregistered — the discovery ended on its own.
                Log.d(TAG, "cancelBrowse: listener already stopped");
            }
        }
    }

    /**
     * Browse for _decentscale._tcp and resolve every instance found.
     *
     * Returns one line per resolved instance:
     *
     *     instanceName\thost\tipv4\tport\tkey=value|key=value
     *
     * Tabs separate fields because a DNS-SD instance label may contain spaces and
     * parentheses ("Half Decent Scale (hdstest)") but not a tab. An unresolved
     * instance contributes no line: a PTR hit is not a device, and reporting one
     * as if it were is what fills a device list with scales that cannot be reached.
     * Returns an empty string when nothing resolved, and null only when NSD itself
     * could not be started — the caller must not render those the same way.
     */
    public static String browseBlocking(final Context ctx, final int timeoutMs) {
        if (ctx == null) {
            Log.w(TAG, "browseBlocking: null context");
            return null;
        }
        final NsdManager nsd = (NsdManager) ctx.getSystemService(Context.NSD_SERVICE);
        if (nsd == null) {
            Log.w(TAG, "browseBlocking: NsdManager unavailable");
            return null;
        }

        cancelBrowse();

        // Keyed by instance name so the same scale answering twice yields one row.
        final Map<String, String> resolved = new ConcurrentHashMap<>();
        final CountDownLatch startFailed = new CountDownLatch(1);

        final NsdManager.DiscoveryListener listener = new NsdManager.DiscoveryListener() {
            @Override
            public void onDiscoveryStarted(String serviceType) {
                Log.d(TAG, "discovery started: " + serviceType);
            }

            @Override
            @SuppressWarnings("deprecation")  // two-arg resolveService; see below
            public void onServiceFound(NsdServiceInfo info) {
                if (info == null || info.getServiceType() == null) return;
                // The two-arg resolveService() is deprecated in API 34 for the
                // Executor variant, which does not exist before API 34. We support
                // API 28+, so the deprecated call is the only one available across
                // the range; the suppression is scoped to this method.
                nsd.resolveService(info, new NsdManager.ResolveListener() {
                    @Override
                    public void onResolveFailed(NsdServiceInfo failed, int errorCode) {
                        // Routine: a stale registration from a scale that rebooted
                        // without sending a goodbye answers the PTR and nothing else.
                        Log.d(TAG, "resolve failed (" + errorCode + ") for "
                                   + (failed != null ? failed.getServiceName() : "?"));
                    }

                    @Override
                    public void onServiceResolved(NsdServiceInfo si) {
                        final String line = format(si);
                        if (line != null) resolved.put(si.getServiceName(), line);
                    }
                });
            }

            @Override public void onServiceLost(NsdServiceInfo info) {
                // Logged, never applied: the list is add-only within one scan, so a
                // scale that blips does not vanish from a list the user is reading.
                Log.d(TAG, "service lost: " + (info != null ? info.getServiceName() : "?"));
            }

            @Override public void onDiscoveryStopped(String serviceType) {}

            @Override
            public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                Log.w(TAG, "start discovery failed: " + errorCode);
                startFailed.countDown();
            }

            @Override
            public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                Log.w(TAG, "stop discovery failed: " + errorCode);
            }
        };

        try {
            synchronized (sDiscoveryLock) {
                nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener);
                sActive = new Active(nsd, listener);
            }
            // Run the full window rather than returning on the first hit: a LAN can
            // hold several scales and the second one is not less real for answering
            // later. If the start failed, give up as soon as we know.
            if (startFailed.await(timeoutMs, TimeUnit.MILLISECONDS)) {
                cancelBrowse();
                return null;
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            Log.w(TAG, "browseBlocking failed: " + e.getMessage());
            cancelBrowse();
            return null;
        }

        cancelBrowse();

        final List<String> lines = new ArrayList<>(resolved.values());
        Collections.sort(lines);
        Log.d(TAG, "browseBlocking: " + lines.size() + " resolved");
        return String.join("\n", lines);
    }

    /** One resolved instance as a tab-separated line, or null if it has no address. */
    private static String format(NsdServiceInfo si) {
        if (si == null) return null;
        final InetAddress addr = si.getHost();
        if (addr == null) return null;
        final String ip = addr.getHostAddress();
        if (ip == null || ip.indexOf(':') >= 0) return null;  // IPv4 only, as elsewhere

        final StringBuilder txt = new StringBuilder();
        final Map<String, byte[]> attrs = si.getAttributes();
        if (attrs != null) {
            for (Map.Entry<String, byte[]> e : attrs.entrySet()) {
                if (e.getKey() == null) continue;
                if (txt.length() > 0) txt.append('|');
                txt.append(e.getKey().toLowerCase()).append('=');
                if (e.getValue() != null) txt.append(new String(e.getValue()));
            }
        }

        // getHostname() is API 34+; the InetAddress carries the SRV target on every
        // level we support, and an empty host is better than a wrong one.
        String host = addr.getHostName();
        if (host == null || host.equals(ip)) host = "";

        return si.getServiceName() + "\t" + host + "\t" + ip + "\t"
             + si.getPort() + "\t" + txt;
    }
}
