package io.github.kulitorum.decenza_de1;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.util.Log;

import java.lang.reflect.Method;
import java.net.InetAddress;
import java.util.ArrayDeque;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Browses for Half Decent Scales with Android's own DNS-SD daemon (NsdManager),
 * as a SECOND path beside the app's mjansson browse — not a replacement.
 *
 * Why a second path, when the app already browses the same service: the two fail
 * for different reasons, and the one that fails is the one we ship.
 *
 * WHAT WAS MEASURED (2026-08-06, one tablet, one LAN, two scales on firmware
 * 3.1.13). Over a single session of several hours, the tablet got 0 records from
 * every A-record query it sent — 7 queries per attempt, 5 s per attempt, every
 * attempt identical — while a Mac on the same LAN resolved the same name in
 * 272 ms with one query. Making the tablet open one TCP connection to the scale
 * flipped it to 1 query / 272 ms, and only for the scale it had contacted: the
 * second scale, never contacted from the tablet, stayed silent to it while
 * answering the Mac normally throughout. (The user-visible outage that led to the
 * investigation had run for five days; the instrumented session is the several
 * hours above. Those are two different numbers and neither substitutes for the
 * other.)
 *
 * SUSPECTED MECHANISM, NOT PROVEN. The app's browse queries from an EPHEMERAL
 * source port, which RFC 6762 section 6.7 makes a "legacy" query that a responder
 * must answer by UNICAST — and to unicast, the scale's lwIP has to resolve the
 * querier's IP to a MAC. ESP-IDF ships ETHARP_TRUST_IP_MAC off, so the scale does
 * not learn the querier's MAC from the multicast query frame it just parsed; it
 * must ARP, and it queues only one packet while that ARP is outstanding. A
 * Wi-Fi power-saving Android tablet is exactly the client that answers that ARP
 * late or not at all. That story fits every measurement above, but no ARP frames
 * were captured and the scale's ARP table was never read, so it stays a
 * hypothesis. It also has a known counter-example on the record: DecentScaleWifi
 * (see the comment above onRecognitionTimeout) documents misses continuing
 * against a scale that had served a WebSocket on its IP 16 s earlier, which a
 * cold ARP entry does not explain. Treat the per-peer BEHAVIOUR as established
 * and the ARP CAUSE as unconfirmed.
 *
 * What is not in doubt is that NsdManager does not share the failure. The system
 * daemon owns port 5353, so its queries are not "legacy" and responses come back
 * MULTICAST to the group. It also sees the scale's unsolicited announcements,
 * which need no query at all. That makes it the path that can find a scale this
 * device has never talked to, which is precisely the case the app's own browse
 * cannot recover on its own.
 *
 * This class replaces an earlier NsdManager helper deleted in #1249 for browsing
 * "_http._tcp" — the wrong service type, because the scale published only a
 * hostname at the time. openscale v3.0.9 added _decentscale._tcp (which is what
 * the app's own browse uses), so the reason that helper was removed no longer
 * holds.
 *
 * USAGE. startBrowse() returns immediately; pollBrowse() hands back one resolved
 * instance at a time as it arrives, blocking at most waitMs; stopBrowse() ends
 * it. The caller polls in slices so it can honour its own cancel token — a
 * blocking call that ran the full window would pin a QThreadPool thread, and
 * ~QCoreApplication waits on that pool unconditionally. Every method is keyed by
 * a caller-supplied token, so concurrent WifiScaleDiscovery instances (the scan
 * and the reconnect ladder each own one) never cancel each other.
 *
 * The multicast lock ShotServer holds for the app lifetime covers reception.
 */
public final class WifiScaleNsdHelper {
    private static final String TAG = "DecenzaWifiNsd";

    // Must match kServiceType on the C++ side. NsdManager wants the trailing dot.
    private static final String SERVICE_TYPE = "_decentscale._tcp.";

    /**
     * First field of a poll line when discovery could not start. Distinguishing
     * "ran and nothing answered" from "never ran" is the whole reason this class
     * reports anything at all; collapsing them hides a broken transport behind
     * "no scales here".
     */
    private static final String FAIL_PREFIX = "!fail";

    private WifiScaleNsdHelper() {}

    private static final Map<Long, Browse> sBrowses = new ConcurrentHashMap<>();

    /**
     * One in-flight browse. Per token, never static: Android allows any number of
     * concurrent DiscoveryListeners (the "already in use" check is per listener
     * OBJECT), so two browses only collide if they share state — which an earlier
     * version of this class did, and which would have made the reconnect ladder's
     * browse silently cancel the user's scan.
     */
    private static final class Browse {
        final NsdManager nsd;
        final LinkedBlockingQueue<String> out = new LinkedBlockingQueue<>();
        final Set<String> reported = ConcurrentHashMap.newKeySet();
        // Resolves are SERIALIZED. NsdManager.resolveService has a single slot on
        // the API levels we support: a second concurrent call fails the new
        // request with FAILURE_ALREADY_ACTIVE, which on a two-scale LAN means the
        // second scale is silently dropped. So instances queue here and are
        // resolved one at a time.
        final ArrayDeque<NsdServiceInfo> pending = new ArrayDeque<>();
        boolean resolveInFlight = false;
        NsdManager.DiscoveryListener listener;
        volatile boolean stopped = false;

        Browse(NsdManager nsd) { this.nsd = nsd; }
    }

    /**
     * Start discovery under `token`. Returns false when NSD is unavailable or the
     * call was rejected outright; an asynchronous start failure arrives instead as
     * a FAIL_PREFIX line from pollBrowse(), so the caller learns about it without
     * waiting out its window.
     */
    public static boolean startBrowse(final Context ctx, final long token) {
        if (ctx == null) {
            Log.w(TAG, "startBrowse: null context");
            return false;
        }
        final NsdManager nsd = (NsdManager) ctx.getSystemService(Context.NSD_SERVICE);
        if (nsd == null) {
            Log.w(TAG, "startBrowse: NsdManager unavailable");
            return false;
        }

        stopBrowse(token);  // a token is never reused, but do not leak if it is

        final Browse b = new Browse(nsd);
        b.listener = new NsdManager.DiscoveryListener() {
            @Override
            public void onDiscoveryStarted(String serviceType) {
                Log.d(TAG, "discovery started: " + serviceType);
            }

            @Override
            public void onServiceFound(NsdServiceInfo info) {
                if (info == null || info.getServiceType() == null) return;
                synchronized (b) {
                    if (b.stopped) return;
                    b.pending.add(info);
                }
                pump(b);
            }

            @Override
            public void onServiceLost(NsdServiceInfo info) {
                // Logged, never applied: the list is add-only within one scan, so a
                // scale that blips does not vanish from a list the user is reading.
                Log.d(TAG, "service lost: " + (info != null ? info.getServiceName() : "?"));
            }

            @Override public void onDiscoveryStopped(String serviceType) {}

            @Override
            public void onStartDiscoveryFailed(String serviceType, int errorCode) {
                Log.w(TAG, "start discovery failed: " + errorCode);
                b.out.offer(FAIL_PREFIX + "\t" + errorCode);
            }

            @Override
            public void onStopDiscoveryFailed(String serviceType, int errorCode) {
                Log.w(TAG, "stop discovery failed: " + errorCode);
            }
        };

        sBrowses.put(token, b);
        try {
            nsd.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, b.listener);
        } catch (Exception e) {
            Log.w(TAG, "discoverServices failed: " + e.getMessage());
            sBrowses.remove(token);
            return false;
        }
        return true;
    }

    /**
     * One resolved instance, or null if none arrived within waitMs (or the token
     * is not running). Lines are:
     *
     *     instanceName\thost\tipv4\tport\tkey=value|key=value
     *
     * Tabs separate fields because a DNS-SD instance label may contain spaces and
     * parentheses ("Half Decent Scale (hdstest)") but not a tab. `host` may be
     * empty — see srvHostname(). An unresolved instance produces no line at all: a
     * PTR hit is not a device, and reporting one as if it were is what fills a
     * device list with scales that cannot be reached.
     */
    public static String pollBrowse(final long token, final int waitMs) {
        final Browse b = sBrowses.get(token);
        if (b == null) return null;
        try {
            return b.out.poll(Math.max(0, waitMs), TimeUnit.MILLISECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        }
    }

    /** Stops the browse under `token`. Safe to call when none is running, and twice. */
    public static void stopBrowse(final long token) {
        final Browse b = sBrowses.remove(token);
        if (b == null) return;
        synchronized (b) {
            b.stopped = true;
            b.pending.clear();
        }
        try {
            b.nsd.stopServiceDiscovery(b.listener);
        } catch (IllegalArgumentException e) {
            // Listener already unregistered — the discovery ended on its own.
            Log.d(TAG, "stopBrowse: listener already stopped");
        }
    }

    /** Issue the next queued resolve, if the single resolve slot is free. */
    @SuppressWarnings("deprecation")  // two-arg resolveService; see below
    private static void pump(final Browse b) {
        final NsdServiceInfo next;
        synchronized (b) {
            if (b.stopped || b.resolveInFlight) return;
            next = b.pending.poll();
            if (next == null) return;
            b.resolveInFlight = true;
        }
        // The two-arg resolveService() is deprecated in API 34 for the Executor
        // variant, which does not exist before API 34. We support API 28+, so the
        // deprecated call is the only one available across the range.
        try {
            b.nsd.resolveService(next, new NsdManager.ResolveListener() {
                @Override
                public void onResolveFailed(NsdServiceInfo failed, int errorCode) {
                    // Routine: a stale registration from a scale that rebooted without
                    // sending a goodbye answers the PTR and nothing else.
                    Log.d(TAG, "resolve failed (" + errorCode + ") for "
                               + (failed != null ? failed.getServiceName() : "?"));
                    release(b);
                }

                @Override
                public void onServiceResolved(NsdServiceInfo si) {
                    final String line = format(si);
                    if (line != null && b.reported.add(si.getServiceName()))
                        b.out.offer(line);
                    release(b);
                }
            });
        } catch (Exception e) {
            // A throw here means no callback will ever arrive, so the slot has to
            // be freed on this path too — otherwise one bad instance wedges the
            // queue and every later scale on the LAN goes unresolved.
            Log.w(TAG, "resolveService threw: " + e.getMessage());
            release(b);
        }
    }

    /** Free the resolve slot and start the next queued instance. */
    private static void release(final Browse b) {
        synchronized (b) { b.resolveInFlight = false; }
        pump(b);
    }

    /** One resolved instance as a tab-separated line, or null if it has no address. */
    @SuppressWarnings("deprecation")  // getHost(); getHostAddresses() is API 34+
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

        return si.getServiceName() + "\t" + srvHostname(si) + "\t" + ip + "\t"
             + si.getPort() + "\t" + txt;
    }

    private static Method sGetHostname;
    private static boolean sGetHostnameResolved = false;

    private static synchronized Method hostnameMethod() {
        if (!sGetHostnameResolved) {
            sGetHostnameResolved = true;
            try {
                sGetHostname = NsdServiceInfo.class.getMethod("getHostname");
            } catch (Throwable t) {
                sGetHostname = null;
            }
        }
        return sGetHostname;
    }

    /**
     * The SRV target ("hds.local"), or "" when this Android version will not say.
     *
     * NEVER InetAddress.getHostName(). NsdServiceInfo travels to us through a
     * Parcel, and NsdServiceInfo.CREATOR rebuilds the address with
     * InetAddress.getByAddress(byte[]) — no host argument (see
     * com.android.net.module.util.InetAddressUtils.unparcelInetAddress). So the
     * address ALWAYS arrives with a null cached hostname, and getHostName() always
     * falls through to a blocking reverse lookup. That lookup goes to the unicast
     * DNS server, never mDNS, so it returns either the IP literal or a router PTR
     * such as "hds.lan" — and "hds.lan" is a different identity key from the
     * mjansson path's "hds.local", which would list the same scale twice and never
     * match a saved "wifi:hds.local" primary.
     *
     * NsdServiceInfo.getHostname() is the real answer and carries the SRV target,
     * but it is API 36 and we compile against 35, hence reflection. It returns the
     * name with ".local." omitted, so the suffix goes back on here to match what
     * MdnsResolver reports.
     *
     * Below API 36 there is no public way to get it, and the caller says so in the
     * log rather than guessing a name.
     */
    private static String srvHostname(NsdServiceInfo si) {
        final Method m = hostnameMethod();
        if (m == null) return "";
        try {
            final Object v = m.invoke(si);
            if (!(v instanceof String)) return "";
            String h = ((String) v).trim();
            while (h.endsWith(".")) h = h.substring(0, h.length() - 1);
            if (h.isEmpty()) return "";
            if (!h.toLowerCase().endsWith(".local")) h = h + ".local";
            return h;
        } catch (Throwable t) {
            return "";
        }
    }
}
