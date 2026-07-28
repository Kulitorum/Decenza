#include "wifiscalediscovery.h"

#include <QHostInfo>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

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
        qDebug() << "[WifiScaleDiscovery] probe timed out with"
                 << m_outstanding << "lookup(s) outstanding";
        emit logMessage(QStringLiteral("mDNS probe timed out"));
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

    qDebug() << "[WifiScaleDiscovery] probing" << hostnames
             << "(timeout" << timeoutMs << "ms)";
    emit logMessage(QString("mDNS lookup of %1 (timeout %2 ms)")
                        .arg(hostnames.join(QStringLiteral(", ")))
                        .arg(timeoutMs));

    for (const QString& hostname : hostnames) {
#ifdef Q_OS_ANDROID
        // Android's stock resolver does not resolve ".local" names, so go
        // direct. resolveHostname() blocks, hence the worker thread. Multicast
        // reception relies on the process-wide WifiManager.MulticastLock that
        // ShotServer holds for the app lifetime.
        QPointer<WifiScaleDiscovery> self(this);
        auto runnable = QRunnable::create([self, hostname, timeoutMs, generation]() {
            const QString ip = MdnsResolver::resolveHostname(hostname, timeoutMs);
            QMetaObject::invokeMethod(qApp, [self, hostname, ip, generation]() {
                if (!self) return;
                if (generation != self->m_probeGeneration) return;  // cancelled/timed out
                if (self->m_outstanding <= 0) return;

                if (ip.isEmpty()) {
                    emit self->logMessage(QStringLiteral("mDNS no responder for ") + hostname);
                } else {
                    WifiScaleResult r;
                    r.foundBy = WifiScaleResult::Source::Fallback;
                    r.hostname = hostname;
                    r.address = ip;
                    emit self->logMessage(QString("mDNS resolved %1 to %2").arg(hostname, ip));
                    emit self->resultFound(r);
                }
                self->finishOneLookup();
            }, Qt::QueuedConnection);
        });
        runnable->setAutoDelete(true);
        QThreadPool::globalInstance()->start(runnable);
#else
        const int id = QHostInfo::lookupHost(hostname, this,
            [this, hostname, generation](const QHostInfo& info) {
                if (generation != m_probeGeneration) return;  // cancelled/timed out
                if (m_outstanding <= 0) return;

                if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                    emit logMessage(QString("mDNS lookup failed for %1: %2")
                                        .arg(hostname, info.errorString()));
                } else {
                    WifiScaleResult r;
                    r.foundBy = WifiScaleResult::Source::Fallback;
                    r.hostname = hostname;
                    r.address = info.addresses().first().toString();
                    emit logMessage(QString("mDNS resolved %1 to %2").arg(hostname, r.address));
                    emit resultFound(r);
                }
                finishOneLookup();
            });
        m_lookupIds.append(id);
#endif
    }

#ifdef Q_OS_ANDROID
    // Watchdog with margin over the workers' own deadline: they normally report
    // first, so this only fires if the thread pool is starved.
    m_timeoutTimer->start(timeoutMs + 1000);
#else
    m_timeoutTimer->start(timeoutMs);
#endif
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

    qDebug() << "[WifiScaleDiscovery] browse" << serviceType
             << "(timeout" << timeoutMs << "ms)";
    emit logMessage(QString("DNS-SD browse for %1 (timeout %2 ms)")
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
                    emit self->logMessage(
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

            // Always report what the browse did. The browse itself runs on a
            // worker thread whose qDebug never reaches the app's debug log, so
            // this is the ONLY place its outcome becomes visible in a log a user
            // can share — and "ran and found nothing", "could not run at all"
            // and "found things but dropped them all as stale" are three very
            // different failures that look identical in the device list.
            emit self->logMessage(
                QString("DNS-SD browse finished via %1 in %2 ms — %3 resolved, "
                        "%4 named but unresolved, %5 withdrawn")
                    .arg(stats.backend.isEmpty() ? QStringLiteral("?") : stats.backend)
                    .arg(stats.elapsedMs)
                    .arg(stats.resolved)
                    .arg(stats.dropped)
                    .arg(stats.withdrawals < 0 ? QStringLiteral("not measured")
                                               : QString::number(stats.withdrawals)));
            if (!stats.error.isEmpty()) {
                emit self->logMessage(
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
}

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
    for (int id : m_lookupIds)
        QHostInfo::abortHostLookup(id);
    m_lookupIds.clear();
    if (m_timeoutTimer) m_timeoutTimer->stop();
}
