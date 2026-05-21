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
        if (!m_androidInFlight) return;
        qDebug() << "[WifiScaleDiscovery] NSD probe timed out for" << m_currentHostname;
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
    // Android's stock resolver (getaddrinfo) does NOT do mDNS for .local
    // names. Use the NsdManager JNI helper instead, on a worker thread so
    // we don't block the Qt event loop while it sits in a CountDownLatch.
    m_androidInFlight = true;
    const int generation = ++m_androidGeneration;

    // Strip ".local" for the service-instance match in Java.
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
        // main thread, and the Java helper blocks on a CountDownLatch.
        QJniObject stemJ = QJniObject::fromString(stemForWorker);
        QJniObject ipJ = QJniObject::callStaticObjectMethod(
            "io/github/kulitorum/decenza_de1/WifiScaleNsdHelper",
            "discoverHdsBlocking",
            "(Ljava/lang/String;I)Ljava/lang/String;",
            stemJ.object<jstring>(),
            jint(timeoutForWorker));
        const QString ip = ipJ.isValid() ? ipJ.toString() : QString();

        // Post the result back to the Qt thread that owns this object. If the
        // object was destroyed in the meantime, the lambda is a no-op (the
        // QPointer is null). If the probe was cancelled/timed out, generation
        // won't match and we drop the result.
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
        //  1. Bump the generation so any worker result that arrives on the
        //     Qt thread after this point is discarded by the equality check
        //     in the queued lambda.
        //  2. Call into the Java helper to stop the active NsdManager
        //     DiscoveryListener registration and count down the worker's
        //     CountDownLatch immediately. Without this eager teardown,
        //     a rapid second probe() would hit FAILURE_ALREADY_ACTIVE
        //     because Android only permits one listener per service type
        //     per NsdManager instance.
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
