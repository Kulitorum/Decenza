#include "wifiscalediscovery.h"

#include <QHostInfo>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include "mdnsresolver.h"
#endif

WifiScaleDiscovery::WifiScaleDiscovery(QObject* parent)
    : QObject(parent)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_ANDROID
        // Watchdog only: the worker's MdnsResolver::resolveHostname() has its
        // own deadline and normally posts a result before this fires. This
        // covers the rare case where the thread-pool worker is starved and
        // never runs. The generation bump in cancelInFlight() drops any late
        // worker result.
        if (!m_androidInFlight) return;
        qDebug() << "[WifiScaleDiscovery] mDNS probe watchdog fired for" << m_currentHostname;
        cancelInFlight();
        emit probeFinished();
#else
        if (m_lookupId == -1) return;
        qDebug() << "[WifiScaleDiscovery] mDNS lookup timed out for" << m_currentHostname;
        cancelInFlight();
        emit probeFinished();
#endif
    });
}

WifiScaleDiscovery::~WifiScaleDiscovery() {
    cancelInFlight();
}

void WifiScaleDiscovery::probe(const QString& hostname, int timeoutMs) {
    cancelInFlight();

    m_currentHostname = hostname;
    qDebug() << "[WifiScaleDiscovery] lookup for" << hostname
             << "(timeout" << timeoutMs << "ms)";

#ifdef Q_OS_ANDROID
    // Android's stock resolver (getaddrinfo, used by QHostInfo) does NOT
    // resolve .local mDNS names. Send a direct mDNS A-record query via the
    // shared MdnsResolver (mjansson/mdns) on a worker thread so the blocking
    // socket poll doesn't stall the Qt event loop. This is the same mechanism
    // MqttClient uses to resolve its .local broker. NOTE: this is hostname
    // resolution, not DNS-SD service discovery — the HDS publishes a .local
    // hostname, not a "_http._tcp" service, so NsdManager is the wrong tool.
    m_androidInFlight = true;
    const int generation = ++m_androidGeneration;

    QPointer<WifiScaleDiscovery> self(this);
    const QString hostnameForWorker = hostname;
    const int timeoutForWorker = timeoutMs;

    auto runnable = QRunnable::create([self, hostnameForWorker,
                                       timeoutForWorker, generation]() {
        // Worker thread — blocks up to timeoutForWorker inside resolveHostname.
        const QString ip = MdnsResolver::resolveHostname(hostnameForWorker, timeoutForWorker);

        // Post the result back to the object's thread. If it was destroyed the
        // QPointer is null and the lambda no-ops; if the probe was cancelled or
        // timed out, the generation won't match and the result is dropped.
        QMetaObject::invokeMethod(qApp, [self, hostnameForWorker, ip, generation]() {
            if (!self) return;
            if (generation != self->m_androidGeneration) return;
            if (!self->m_androidInFlight) return;

            self->m_androidInFlight = false;
            self->m_timeoutTimer->stop();

            if (ip.isEmpty()) {
                qDebug() << "[WifiScaleDiscovery] mDNS lookup failed for" << hostnameForWorker;
                emit self->probeFinished();
                return;
            }
            qDebug() << "[WifiScaleDiscovery] mDNS found" << hostnameForWorker
                     << "->" << ip;
            emit self->scaleFound(hostnameForWorker, ip);
            emit self->probeFinished();
        }, Qt::QueuedConnection);
    });
    runnable->setAutoDelete(true);
    QThreadPool::globalInstance()->start(runnable);

    m_timeoutTimer->start(timeoutMs);
#else
    m_lookupId = QHostInfo::lookupHost(hostname, this,
        [this](const QHostInfo& info) {
            // Late callback after cancel/timeout — drop it.
            if (m_lookupId == -1) return;
            m_lookupId = -1;
            m_timeoutTimer->stop();

            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                qDebug() << "[WifiScaleDiscovery] lookup failed for"
                         << m_currentHostname << "-" << info.errorString();
                emit probeFinished();
                return;
            }

            const QString resolved = info.addresses().first().toString();
            qDebug() << "[WifiScaleDiscovery] found" << m_currentHostname
                     << "->" << resolved;
            emit scaleFound(m_currentHostname, resolved);
            emit probeFinished();
        });

    m_timeoutTimer->start(timeoutMs);
#endif
}

void WifiScaleDiscovery::cancelInFlight() {
#ifdef Q_OS_ANDROID
    if (m_androidInFlight) {
        // Bump the generation so any worker result that arrives on the Qt
        // thread after this point is discarded by the equality check in the
        // queued lambda. We can't synchronously interrupt the worker's
        // resolveHostname() (it's blocked in select() up to its own deadline),
        // but it closes its own mDNS socket on return and the late result is
        // dropped — no shared listener/registration to tear down.
        ++m_androidGeneration;
        m_androidInFlight = false;
    }
#endif
    if (m_lookupId != -1) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    if (m_timeoutTimer) m_timeoutTimer->stop();
}
