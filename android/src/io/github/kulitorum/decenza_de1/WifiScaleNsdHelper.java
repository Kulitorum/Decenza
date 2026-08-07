package io.github.kulitorum.decenza_de1;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Build;
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
 * WHY THIS EXISTS, AND WHAT IT DOES NOT FIX. An Android tablet got 0 records from
 * every A-record query it sent — 7 queries per attempt, every attempt identical
 * across hours — while a Mac on the same LAN resolved the same name in 272 ms.
 *
 * Resolution is PER PEER, and outbound IP traffic to a given scale is what enables
 * it. From a cold tablet: TCP to the gateway changed nothing (0/6, 0/6), so it is
 * not a device-wide wake; a FAILED TCP connect to one scale moved that scale to
 * 4/6 while the other stayed 0/6; a failed connect to the other then moved it to
 * 5/6 while the first held 6/6. Each scale is the other's control, both directions.
 *
 * The mechanism is inferred, not captured: the outbound packet makes the TABLET ARP
 * for the scale, the scale answers and caches our MAC, and only then can it answer
 * a legacy (ephemeral source port) query by the unicast RFC 6762 6.7 requires —
 * which is exactly what Android's resolver sends, verified on the wire. Entries age
 * out (ARP_MAXAGE, 300 s), which is why a scale that worked minutes ago goes quiet.
 * Two other accounts (scale-side power save, tablet-side power save) were asserted
 * and refuted first; docs/WIFI_SCALE_MDNS.md has the measurements and both
 * retractions. Read it before adding a third.
 *
 * That is the case FOR this path rather than against it: a query from port 5353 is
 * answered MULTICAST, which needs no ARP and so reaches a scale this device has
 * never addressed. It needs a held MulticastLock to be received at all, which is
 * why the app now takes its own.
 *
 * IT HAS NOT BEEN SHOWN TO HELP. On-device, a never-contacted scale stayed
 * invisible to NsdManager throughout, which is the exact case this class was added
 * for. Every measurement so far was taken while the app's own socket was bound to
 * 5353 and starving the daemon this depends on, so the value is UNPROVEN rather
 * than disproven. Retained pending a fair test on a build that no longer binds
 * 5353; if that test finds nothing, delete this class rather than re-justifying it.
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
 * Multicast reception needs a held WifiManager.MulticastLock. The app takes its
 * own, reference-counted, for the duration of each lookup and browse — see
 * MulticastLock. Do NOT write that ShotServer holds one for the app lifetime:
 * that claim was false in three other files before this one, its setting
 * defaults to off, and it is what this branch spent a day disproving.
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
        // Everything NsdManager told us, whether or not it survived to pollBrowse().
        // Without these the C++ summary counts only lines that crossed JNI, so
        // "the LAN is empty" and "we found scales and discarded every one" are the
        // same log line -- the distinction the mjansson path reports as
        // instancesSeen and the spec requires of both paths.
        int seen = 0;             // onServiceFound calls
        int resolveFailed = 0;    // onResolveFailed + resolveService throws
        int noAddress = 0;        // resolved but no usable IPv4
        // The ResolveListener currently registered with the framework, if any.
        // Held ONLY so stopBrowse() can retract it: a resolution registered when
        // the DiscoveryListener goes away outlives its client, and the framework
        // logs "NsdService: id <n> for <n> has no client mapping" on teardown.
        NsdManager.ResolveListener resolveListener;
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
                    b.seen++;
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

    /**
     * Tab-separated "seen\tresolveFailed\tnoAddress" for `token`, or "" if the
     * browse is unknown. Read by the C++ side BEFORE stopBrowse(), so its summary
     * line can distinguish an empty LAN from a browse that found scales and
     * discarded every one of them. Java-side Log.* never reaches a submitted log
     * -- AsyncLogger forwards Qt messages OUT to logcat and never reads logcat
     * back in -- so anything a field diagnosis needs has to cross JNI like this.
     */
    public static String browseCounters(final long token) {
        final Browse b = sBrowses.get(token);
        if (b == null) return "";
        synchronized (b) {
            return b.seen + "\t" + b.resolveFailed + "\t" + b.noAddress;
        }
    }

    /** Stops the browse under `token`. Safe to call when none is running, and twice. */
    public static void stopBrowse(final long token) {
        final Browse b = sBrowses.remove(token);
        if (b == null) return;
        final NsdManager.ResolveListener pendingResolve;
        synchronized (b) {
            b.stopped = true;
            b.pending.clear();
            pendingResolve = b.resolveInFlight ? b.resolveListener : null;
            b.resolveListener = null;
            b.resolveInFlight = false;
        }

        // Retract the resolution BEFORE the discovery it belongs to. A resolve
        // still registered when its DiscoveryListener is unregistered is left
        // parented to a client that no longer exists, and the framework logs
        // "NsdService: id <n> for <n> has no client mapping" — observed on every
        // teardown that stopped mid-resolve. Reversing these two calls is the bug.
        //
        // stopServiceResolution() is API 34+; below that the framework offers no
        // way to cancel one, so a resolve started there still runs to its own
        // timeout. Nothing leaks either way — b.stopped makes the callback a
        // no-op — but the framework log line is unavoidable on older devices.
        if (pendingResolve != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            try {
                b.nsd.stopServiceResolution(pendingResolve);
            } catch (IllegalArgumentException e) {
                // Already completed between our check and this call — benign.
                Log.d(TAG, "stopBrowse: resolve already finished");
            }
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
        final NsdManager.ResolveListener rl = new NsdManager.ResolveListener() {
            @Override
            public void onResolveFailed(NsdServiceInfo failed, int errorCode) {
                // Routine: a stale registration from a scale that rebooted without
                // sending a goodbye answers the PTR and nothing else.
                Log.d(TAG, "resolve failed (" + errorCode + ") for "
                           + (failed != null ? failed.getServiceName() : "?"));
                synchronized (b) { b.resolveFailed++; }
                release(b);
            }

            @Override
            public void onServiceResolved(NsdServiceInfo si) {
                final String line = format(si);
                if (line == null) {
                    // Resolved, but unusable: no address, or IPv6 only. Counted so
                    // the C++ summary can say so -- returning null here is otherwise
                    // indistinguishable from "nothing arrived this slice".
                    synchronized (b) { b.noAddress++; }
                    Log.d(TAG, "resolved but no usable IPv4: "
                               + (si != null ? si.getServiceName() : "?"));
                } else {
                    // getServiceName() is the dedupe key and reported is a
                    // ConcurrentHashMap keySet, which rejects null.
                    final String name = si.getServiceName();
                    if (name != null && b.reported.add(name))
                        b.out.offer(line);
                }
                release(b);
            }
        };
        // stopBrowse() can land between reserving the slot above and registering
        // here. It would see resolveInFlight with a null listener, have nothing to
        // retract, and we would then register a resolution against a browse that is
        // already torn down -- reintroducing the orphan this method exists to avoid.
        // Registering INSIDE the lock is what makes stopBrowse() correct. Storing
        // the listener under the lock and then calling out to the framework
        // unlocked still leaves a window: stopBrowse() could run in between, call
        // stopServiceResolution() on a listener the framework has never seen (which
        // throws, and is swallowed), unregister the DiscoveryListener, and drop the
        // token -- and then we would register rl against a torn-down browse. That
        // recreates the orphaned resolution, and on API < 34 nothing can cancel it.
        // Java monitors are reentrant, so a callback delivered on this thread is
        // safe; NsdManager delivers on its own handler thread anyway.
        boolean threw = false;
        synchronized (b) {
            if (b.stopped) {
                b.resolveInFlight = false;
                return;
            }
            b.resolveListener = rl;
            try {
                b.nsd.resolveService(next, rl);
            } catch (Exception e) {
                // A throw here means no callback will ever arrive, so the slot has
                // to be freed on this path too — otherwise one bad instance wedges
                // the queue and every later scale on the LAN goes unresolved.
                Log.w(TAG, "resolveService threw: " + e.getMessage());
                b.resolveFailed++;
                b.resolveInFlight = false;
                b.resolveListener = null;
                threw = true;
            }
        }
        if (threw)
            pump(b);
    }

    /** Free the resolve slot and start the next queued instance. */
    private static void release(final Browse b) {
        synchronized (b) {
            b.resolveInFlight = false;
            b.resolveListener = null;  // retired by the framework; nothing to retract
        }
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
            // NOT the same as "this Android is too old" -- hostnameMethod()'s null
            // check already covers that, and the C++ side reports an empty return
            // as "needs API 36+". A throw HERE is a real reflection failure on this
            // device, and reporting it as a platform-version limit would send the
            // reader looking at the wrong thing entirely.
            Log.w(TAG, "srvHostname reflection failed on API "
                       + android.os.Build.VERSION.SDK_INT + ": " + t);
            return "";
        }
    }
}
