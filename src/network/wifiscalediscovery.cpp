#include "wifiscalediscovery.h"

#include "ble/scales/scalelogging.h"

#include <QHostInfo>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#ifdef Q_OS_ANDROID
#include <QElapsedTimer>
#include <QJniObject>
#include <QMap>
#include <QtCore/private/qandroidextras_p.h>
#endif

#include "mdnsresolver.h"

QStringList WifiScaleDiscovery::defaultFallbackHostnames()
{
    // See the header for why "-2"/"-3" are here and why they are NOT protocol.
    return {QStringLiteral("hds.local"),
            QStringLiteral("hds-2.local"),
            QStringLiteral("hds-3.local")};
}

WifiScaleDiscovery::WifiScaleDiscovery(QObject* parent)
    : QObject(parent)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_outstanding <= 0) return;
        // Logs at source through the shared helper: one call writes stderr at
        // the right severity AND emits for the connections view, so the tier this
        // class chooses survives. Forwarding a bare string through BLEManager
        // could not carry it, and the outcomes below are required to be
        // diagnosable from a user-shared log (see the wifi-scale-discovery spec).
        // INFO, not WARN, for the same reason as the per-name failure below:
        // a probe timing out with lookups outstanding is the ordinary outcome on
        // a network with no HDS scale on it, and WARN would cry wolf on every
        // scan. The spec only requires it be recorded distinguishably.
        SCALE_INFO_TAGGED("WifiScaleDiscovery",
            QString("mDNS probe timed out with %1 lookup(s) outstanding")
                .arg(m_outstanding));
        const bool ran = m_anyProbeRan;
        cancelInFlight();
        emit probeFinished(ran);
    });
}

WifiScaleDiscovery::~WifiScaleDiscovery() {
    cancelInFlight();
    // Don't emit from a destructor; just release the worker so shutdown isn't
    // held up by a browse running out its deadline.
    if (m_browseCancel)
        m_browseCancel->store(true, std::memory_order_relaxed);
    m_browseCancel.reset();
    m_browseInFlight = false;
}

void WifiScaleDiscovery::probe(const QString& hostname, int timeoutMs) {
    probe(QStringList{hostname}, timeoutMs);
}

void WifiScaleDiscovery::probe(const QStringList& hostnames, int timeoutMs) {
    cancelInFlight();

    if (hostnames.isEmpty()) {
        // Nothing to do — report "did not run" so the caller can distinguish
        // this from a probe that ran and found nothing.
        emit probeFinished(false);
        return;
    }

    m_anyProbeRan = true;
    m_outstanding = static_cast<int>(hostnames.size());
    const int generation = ++m_probeGeneration;
    // Fresh token per probe; shared_ptr so a worker outliving this object never
    // dereferences freed memory.
    m_probeCancel = std::make_shared<std::atomic<bool>>(false);

    SCALE_INFO_TAGGED("WifiScaleDiscovery",
        QString("mDNS lookup of %1 (timeout %2 ms)")
            .arg(hostnames.join(QStringLiteral(", ")))
            .arg(timeoutMs));

    // Which of the two lookup paths runs. This used to be `#ifdef Q_OS_ANDROID`,
    // and the default is unchanged — mjansson on Android, the system resolver
    // everywhere else. What the runtime check buys is that a desktop build can be
    // pointed at Android's exact A-record path (devices_wifi resolver=mjansson),
    // which is the only way to tell a backend fault from a device fault without
    // deploying to a device. The browse half already worked this way; this is the
    // resolve half catching up. See MdnsResolver::HostnameResolver.
    const bool direct = MdnsResolver::useDirectHostnameResolver();

    for (const QString& hostname : hostnames) {
        if (direct) {
            // Android's stock resolver does not resolve ".local" names, so go
            // direct. resolveHostname() blocks, hence the worker thread. It takes
            // its own MulticastLock for the duration; it used to rely on
            // ShotServer holding one app-wide, which was never true — that
            // setting defaults to off.
            QPointer<WifiScaleDiscovery> self(this);
            auto cancel = m_probeCancel;
            auto runnable = QRunnable::create([self, hostname, timeoutMs, generation, cancel]() {
                MdnsResolver::ResolveStats rs;
                const QString ip = MdnsResolver::resolveHostname(hostname, timeoutMs,
                                                                 &rs, cancel.get());
                // STDERR_TAGGED, not the emitting SCALE_INFO_TAGGED used elsewhere in this
                // file: these lambdas run detached from any `this`, reaching the object
                // only through the QPointer `self` the null-check above guards. The macro's
                // `emit logMessage(...)` needs `this` in scope, which none of these six
                // sites has. (There is also no longer anything to emit to — the last
                // connection to this class's logMessage signal was removed elsewhere in
                // this change, which is why the signal itself was deleted rather than kept
                // for a listener that no longer exists.)
                QMetaObject::invokeMethod(qApp, [self, hostname, ip, generation, rs]() {
                    if (!self) return;
                    if (generation != self->m_probeGeneration) return;  // cancelled/timed out
                    if (self->m_outstanding <= 0) return;

                    if (ip.isEmpty()) {
                        // "Nobody answered" and "we never managed to ask" have
                        // different fixes — multicast lock / permissions versus a
                        // sleeping scale or the wrong SSID — so say which happened
                        // instead of logging one guess for both.
                        if (!rs.error.isEmpty()) {
                            SCALE_WARN_STDERR_TAGGED("WifiScaleDiscovery",
                                QString("mDNS lookup of %1 could not run: %2").arg(hostname, rs.error));
                        } else {
                            SCALE_INFO_STDERR_TAGGED("WifiScaleDiscovery",
                                QString("mDNS no responder for %1 (%2 queries sent, %3 records seen)")
                                    .arg(hostname).arg(rs.queries).arg(rs.recordsSeen));
                        }
                    } else {
                        WifiScaleResult r;
                        r.foundBy = WifiScaleResult::Source::Fallback;
                        r.hostname = hostname;
                        r.address = ip;
                        SCALE_INFO_STDERR_TAGGED("WifiScaleDiscovery",
                            QString("mDNS resolved %1 to %2").arg(hostname, ip));
                        emit self->resultFound(r);
                    }
                    self->finishOneLookup();
                }, Qt::QueuedConnection);
            });
            runnable->setAutoDelete(true);
            QThreadPool::globalInstance()->start(runnable);
        } else {
            const int id = QHostInfo::lookupHost(hostname, this,
                [this, hostname, generation](const QHostInfo& info) {
                    if (generation != m_probeGeneration) return;  // cancelled/timed out
                    if (m_outstanding <= 0) return;

                    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                        // INFO, not WARN: hds-2/hds-3 are absent on most networks,
                        // so a per-name failure is the normal case. The spec needs it
                        // recorded per name so a partial result is diagnosable.
                        SCALE_INFO_TAGGED("WifiScaleDiscovery",
                            QString("mDNS lookup failed for %1: %2")
                                .arg(hostname, info.errorString()));
                    } else {
                        WifiScaleResult r;
                        r.foundBy = WifiScaleResult::Source::Fallback;
                        r.hostname = hostname;
                        r.address = info.addresses().first().toString();
                        SCALE_INFO_TAGGED("WifiScaleDiscovery",
                            QString("mDNS resolved %1 to %2").arg(hostname, r.address));
                        emit resultFound(r);
                    }
                    finishOneLookup();
                });
            m_lookupIds.append(id);
        }
    }

    // Watchdog with margin over the workers' own deadline when they own the
    // deadline: the direct path's workers normally report first, so the extra
    // second only matters if the thread pool is starved. QHostInfo has no
    // deadline of its own, so its branch gets the bare timeout.
    m_timeoutTimer->start(direct ? timeoutMs + 1000 : timeoutMs);
}

void WifiScaleDiscovery::finishOneLookup() {
    if (--m_outstanding > 0)
        return;
    m_timeoutTimer->stop();
    m_lookupIds.clear();
    const bool ran = m_anyProbeRan;
    m_anyProbeRan = false;
    emit probeFinished(ran);
}

void WifiScaleDiscovery::browse(int timeoutMs) {
    stopBrowse();

    m_browseInFlight = true;
    const int generation = ++m_browseGeneration;
    const QString serviceType = QString::fromLatin1(kServiceType);
    // Shared with the worker so stopBrowse() can end it early. shared_ptr, not a
    // member pointer, so a worker outliving this object never dereferences freed
    // memory.
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_browseCancel = cancel;

    SCALE_INFO_TAGGED("WifiScaleDiscovery",
        QString("DNS-SD browse for %1 (timeout %2 ms)")
            .arg(serviceType).arg(timeoutMs));

    QPointer<WifiScaleDiscovery> self(this);
    auto runnable = QRunnable::create([self, serviceType, timeoutMs, generation, cancel]() {
        // Runs on a worker thread — browseService() blocks until its deadline.
        // The onResolved callback also fires on this thread, so each result is
        // marshalled to the object's thread individually. That is what makes
        // rows appear as scales answer instead of all at once at the end.
        MdnsResolver::BrowseStats stats;
        MdnsResolver::browseService(serviceType, timeoutMs,
            [self, generation](const MdnsResolver::ServiceInstance& si) {
                QMetaObject::invokeMethod(qApp, [self, si, generation]() {
                    if (!self) return;
                    if (generation != self->m_browseGeneration) return;  // stopped
                    if (!self->m_browseInFlight) return;

                    const WifiScaleResult r = WifiScaleResultUtil::fromBrowseTxt(
                        si.instanceName, si.hostname, si.address, si.port, si.txt);
                    SCALE_INFO_STDERR_TAGGED("WifiScaleDiscovery",
                        QString("DNS-SD found %1 at %2 (%3) fw=%4")
                            .arg(r.instanceName.isEmpty() ? r.hostname : r.instanceName,
                                 r.address, r.hostname,
                                 r.firmwareVersion.isEmpty() ? QStringLiteral("?")
                                                             : r.firmwareVersion));
                    emit self->resultFound(r);
                }, Qt::QueuedConnection);
            },
            &stats, cancel.get());

        QMetaObject::invokeMethod(qApp, [self, generation, stats]() {
            if (!self) return;
            if (generation != self->m_browseGeneration) return;
            if (!self->m_browseInFlight) return;
            self->m_browseInFlight = false;

            // Always report what the browse did. "Ran and found nothing",
            // "could not run at all" and "found things but dropped them all as
            // stale" are three very different failures that look identical in
            // the device list, so the outcome has to reach the shareable log as
            // data rather than being inferred from its absence.
            SCALE_INFO_STDERR_TAGGED("WifiScaleDiscovery",
                QString("DNS-SD browse finished via %1 in %2 ms — %3 resolved, "
                        "%4 named but unresolved, %5 withdrawn")
                    .arg(stats.backend.isEmpty() ? QStringLiteral("?") : stats.backend)
                    .arg(stats.elapsedMs)
                    .arg(stats.resolved)
                    .arg(stats.dropped)
                    .arg(stats.withdrawals < 0 ? QStringLiteral("not measured")
                                               : QString::number(stats.withdrawals)));
            if (!stats.error.isEmpty()) {
                SCALE_WARN_STDERR_TAGGED("WifiScaleDiscovery",
                    QStringLiteral("DNS-SD browse ERROR: ") + stats.error);
            }
            // `ran` is false when the browse could not actually run. Reporting
            // true here regardless — as an earlier version did — made the flag
            // structurally incapable of being false and defeated its purpose.
            emit self->browseFinished(stats.error.isEmpty());
        }, Qt::QueuedConnection);
    });
    runnable->setAutoDelete(true);
    QThreadPool::globalInstance()->start(runnable);

    startNsdBrowse(timeoutMs, generation);
}

WifiScaleDiscovery::NsdLine WifiScaleDiscovery::parseNsdLine(const QString& line) {
    NsdLine out;
    // KeepEmptyParts: `host` is legitimately empty below API 36, and dropping
    // empty fields would slide `ipv4` into its place — the scale's address then
    // read as its hostname, which fails silently rather than loudly.
    const QStringList f = line.split(QLatin1Char('\t'));
    if (f.value(0) == QLatin1String("!fail")) {
        out.startFailure = true;
        out.failureCode = f.value(1);
        return out;
    }
    if (f.size() < 5)
        return out;  // truncated — not an instance, and not a failure either

    out.instanceName = f[0];
    out.hostname = f[1];
    out.address = f[2];
    out.port = static_cast<quint16>(f[3].toUShort());

    const QStringList pairs = f[4].split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString& kv : pairs) {
        const qsizetype eq = kv.indexOf(QLatin1Char('='));
        if (eq > 0) out.txt.insert(kv.left(eq).toLower(), kv.mid(eq + 1));
    }
    // An address is the one field that cannot be recovered from anywhere else.
    out.valid = !out.address.isEmpty();
    return out;
}

#ifdef Q_OS_ANDROID
void WifiScaleDiscovery::startNsdBrowse(int timeoutMs, int generation) {
    // Android's own DNS-SD daemon, running BESIDE the mjansson browse above and
    // reporting into the same resultFound stream. Not a fallback chained after it:
    // both start together, because the case this exists for is one where the
    // mjansson browse returns cleanly and empty, which is indistinguishable from
    // an empty LAN and so cannot be used as a trigger.
    //
    // The two fail for unrelated reasons, which is the whole value of running
    // both, and the reasons are opposite. A unicast reply is acked and retried by
    // the AP, so it survives a lossy WLAN better — but the scale can only send it
    // once it holds this device's MAC, which measurably it does not until we have
    // sent it IP traffic. A multicast reply needs no MAC and so reaches a scale
    // this device has never addressed, but is fire-and-forget and, on Android,
    // needs a held MulticastLock.
    //
    // So the deterministic failure (a tablet getting nothing for hours while a Mac
    // resolves the same name) is a per-peer, unicast-path failure, and NsdManager's
    // multicast path is the one that can recover from it. See
    // docs/WIFI_SCALE_MDNS.md for the controls that established this and for the
    // two accounts refuted along the way.
    //
    // Deduplication is the caller's, by HOSTNAME — see
    // WifiScaleResultUtil::upsertByHostname. The same scale answering both paths
    // is the expected case, not an error, which is why the Java side goes to some
    // trouble to report the SRV target rather than any other name for the host.
    auto cancel = m_browseCancel;
    if (!cancel) return;  // browse() always sets it; nothing to run without one

    // Per-browse handle. The Java side keys everything by this, so the scan's
    // instance and the reconnect ladder's instance never stop each other — an
    // invariant this class's header states and an earlier draft of the helper
    // broke by holding one static listener.
    static std::atomic<qint64> s_nextNsdToken{1};
    const qint64 token = s_nextNsdToken.fetch_add(1, std::memory_order_relaxed);

    QPointer<WifiScaleDiscovery> self(this);
    auto runnable = QRunnable::create([self, timeoutMs, generation, cancel, token]() {
        // STDERR_TAGGED throughout, never the emitting SCALE_INFO_TAGGED: this
        // lambda and the ones it posts are detached from any `this` and reach the
        // object only through the QPointer. The emitting macro needs `this` in
        // scope and does not compile here — see the longer note in probe().
        const char* const kHelper = "io/github/kulitorum/decenza_de1/WifiScaleNsdHelper";

        QJniObject ctx = QNativeInterface::QAndroidApplication::context();
        if (!ctx.isValid()) {
            SCALE_WARN_STDERR_TAGGED("WifiScaleDiscovery",
                QStringLiteral("NSD browse: no Android context"));
            return;
        }

        const bool started = QJniObject::callStaticMethod<jboolean>(
            kHelper, "startBrowse", "(Landroid/content/Context;J)Z",
            ctx.object(), static_cast<jlong>(token));
        if (!started) {
            // "Could not start" and "started and nobody answered" have completely
            // different fixes, and both show a user an empty list. Say which.
            SCALE_WARN_STDERR_TAGGED("WifiScaleDiscovery",
                QStringLiteral("NSD browse could not run (NsdManager unavailable "
                               "or discoverServices rejected)"));
            return;
        }

        // Poll in slices rather than blocking for the whole window. Two reasons:
        // results reach the device list as each scale answers instead of all at
        // once at the deadline, and the cancel token is honoured within one slice.
        // A worker that ran the full window would pin a QThreadPool thread, and
        // ~QCoreApplication calls waitForDone() unconditionally — that is a
        // multi-second hang on quit with the UI already gone.
        constexpr int kPollSliceMs = 400;
        const int sdk = QNativeInterface::QAndroidApplication::sdkVersion();
        QElapsedTimer clock;
        clock.start();
        int resolved = 0;
        int unnamed = 0;

        while (!cancel->load(std::memory_order_relaxed) && clock.elapsed() < timeoutMs) {
            const int remaining = static_cast<int>(timeoutMs - clock.elapsed());
            const QJniObject lineObj = QJniObject::callStaticObjectMethod(
                kHelper, "pollBrowse", "(JI)Ljava/lang/String;",
                static_cast<jlong>(token), static_cast<jint>(qMin(kPollSliceMs, remaining)));
            if (!lineObj.isValid())
                continue;  // nothing this slice

            const NsdLine parsed = WifiScaleDiscovery::parseNsdLine(lineObj.toString());
            if (parsed.startFailure) {
                SCALE_WARN_STDERR_TAGGED("WifiScaleDiscovery",
                    QString("NSD browse failed to start (NsdManager error %1)")
                        .arg(parsed.failureCode));
                break;
            }
            if (!parsed.valid) continue;

            const QString instanceName = parsed.instanceName;
            const QString host = parsed.hostname;
            const QString ip = parsed.address;
            const quint16 port = parsed.port;

            if (host.isEmpty()) {
                // NsdServiceInfo.getHostname() is API 36; below that Android
                // exposes no SRV target at all, and the identity key every result
                // is stored under IS the hostname. Reporting a guessed name would
                // poison the saved "wifi:<hostname>" primary, so the row is
                // dropped — loudly, with the one thing a reader needs to know.
                ++unnamed;
                SCALE_WARN_STDERR_TAGGED("WifiScaleDiscovery",
                    QString("NSD resolved '%1' at %2 but this Android (API %3) does not "
                            "expose the mDNS hostname — needs API 36+; add the scale by "
                            "IP if it does not appear otherwise")
                        .arg(instanceName, ip).arg(sdk));
                continue;
            }

            const QMap<QString, QString> txt = parsed.txt;
            ++resolved;

            QMetaObject::invokeMethod(qApp, [self, instanceName, host, ip, port, txt, generation]() {
                if (!self) return;
                if (generation != self->m_browseGeneration) return;
                const WifiScaleResult r = WifiScaleResultUtil::fromBrowseTxt(
                    instanceName, host, ip, port, txt);
                SCALE_INFO_STDERR_TAGGED("WifiScaleDiscovery",
                    QString("NSD found %1 at %2 (%3) fw=%4")
                        .arg(r.instanceName.isEmpty() ? r.hostname : r.instanceName,
                             r.address, r.hostname,
                             r.firmwareVersion.isEmpty() ? QStringLiteral("?")
                                                         : r.firmwareVersion));
                emit self->resultFound(r);
            }, Qt::QueuedConnection);
        }

        QJniObject::callStaticMethod<void>(kHelper, "stopBrowse", "(J)V",
                                           static_cast<jlong>(token));

        SCALE_INFO_STDERR_TAGGED("WifiScaleDiscovery",
            QString("NSD browse finished in %1 ms — %2 resolved, %3 dropped for want "
                    "of a hostname").arg(clock.elapsed()).arg(resolved).arg(unnamed));
    });
    runnable->setAutoDelete(true);
    QThreadPool::globalInstance()->start(runnable);
}
#else
void WifiScaleDiscovery::startNsdBrowse(int, int) {
    // Non-Android: the system resolver already speaks mDNS through a daemon that
    // owns 5353, so there is no second path to add — QHostInfo and Bonjour are
    // that path. See mdnsresolver.h.
}
#endif

void WifiScaleDiscovery::stopBrowse() {
    if (!m_browseInFlight)
        return;
    // Signal the worker to stop rather than letting it run out its deadline —
    // it holds a pool thread, and app shutdown waits on the pool.
    if (m_browseCancel)
        m_browseCancel->store(true, std::memory_order_relaxed);
    m_browseCancel.reset();
    // Bump the generation so any callbacks still in flight are dropped.
    ++m_browseGeneration;
    m_browseInFlight = false;
    // The worker's own completion is now discarded by the generation check, so
    // emit the terminal signal here or callers waiting on it would hang.
    emit browseFinished(false);
}

void WifiScaleDiscovery::cancelInFlight() {
    if (m_outstanding > 0) {
        ++m_probeGeneration;
        m_outstanding = 0;
        m_anyProbeRan = false;
    }
    // Release the Android workers rather than only discarding their results.
    if (m_probeCancel)
        m_probeCancel->store(true, std::memory_order_relaxed);
    m_probeCancel.reset();
    for (int id : m_lookupIds)
        QHostInfo::abortHostLookup(id);
    m_lookupIds.clear();
    if (m_timeoutTimer) m_timeoutTimer->stop();
}
