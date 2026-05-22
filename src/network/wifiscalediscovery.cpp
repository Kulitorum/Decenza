#include "wifiscalediscovery.h"

#include <QHostInfo>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#endif

WifiScaleDiscovery::WifiScaleDiscovery(QObject* parent)
    : QObject(parent)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_ANDROID
        // Watchdog only: the worker's NsdManager discovery has its own deadline
        // (the CountDownLatch await) and normally posts a result before this
        // fires. This covers the rare case where the thread-pool worker is
        // starved and never runs. The generation bump in cancelInFlight() drops
        // any late worker result.
        if (!m_androidInFlight) return;
        qDebug() << "[WifiScaleDiscovery] NSD probe watchdog fired for" << m_currentHostname;
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
    // Android's stock resolver (getaddrinfo) does NOT do mDNS for .local names,
    // and a direct app-socket mDNS query can't receive the reply (the OS mDNS
    // daemon monopolizes port-5353 multicast; unicast replies get dropped).
    // The scale firmware advertises a _decentscale._tcp DNS-SD service, so we
    // discover it via NsdManager (which runs inside the OS mDNS daemon — the
    // only path that reliably receives mDNS on Android). The JNI helper blocks
    // on a CountDownLatch, so run it on a worker thread to keep the Qt event
    // loop responsive.
    m_androidInFlight = true;
    const int generation = ++m_androidGeneration;

    // Strip ".local" — passed to the helper for logging only; the dedicated
    // _decentscale._tcp service type is the actual match.
    QString stem = hostname;
    if (stem.endsWith(QStringLiteral(".local"), Qt::CaseInsensitive)) {
        stem.chop(QStringLiteral(".local").size());
    }

    QPointer<WifiScaleDiscovery> self(this);
    const QString stemForWorker = stem;
    const QString hostnameForWorker = hostname;
    const int timeoutForWorker = timeoutMs;

    auto runnable = QRunnable::create([self, stemForWorker, hostnameForWorker,
                                       timeoutForWorker, generation]() {
        // Runs on a QThreadPool worker thread — JNI calls are allowed off the
        // main thread, and the Java helper blocks on a CountDownLatch. The
        // Android Context is passed in because Qt's QtNative.getContext() is
        // package-private and unavailable from our Java package.
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject stemJ = QJniObject::fromString(stemForWorker);
        QJniObject ipJ = QJniObject::callStaticObjectMethod(
            "io/github/kulitorum/decenza_de1/WifiScaleNsdHelper",
            "discoverHdsBlocking",
            "(Landroid/content/Context;Ljava/lang/String;I)Ljava/lang/String;",
            context.object(),
            stemJ.object<jstring>(),
            jint(timeoutForWorker));
        const QString ip = ipJ.isValid() ? ipJ.toString() : QString();

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
                qDebug() << "[WifiScaleDiscovery] NSD lookup failed for" << hostnameForWorker;
                emit self->probeFinished();
                return;
            }
            qDebug() << "[WifiScaleDiscovery] NSD found" << hostnameForWorker
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
        // Two-part cancel:
        //  1. Bump the generation so any worker result that arrives on the Qt
        //     thread after this point is discarded by the equality check in the
        //     queued lambda.
        //  2. Call into the Java helper to stop the active NsdManager
        //     DiscoveryListener registration and count down the worker's
        //     CountDownLatch immediately. Without this eager teardown, a rapid
        //     second probe() would hit FAILURE_ALREADY_ACTIVE because Android
        //     only permits one listener per service type per NsdManager instance.
        ++m_androidGeneration;
        m_androidInFlight = false;
        QJniObject::callStaticMethod<void>(
            "io/github/kulitorum/decenza_de1/WifiScaleNsdHelper",
            "cancelDiscovery", "()V");
    }
#endif
    if (m_lookupId != -1) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    if (m_timeoutTimer) m_timeoutTimer->stop();
}
