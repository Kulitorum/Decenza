#include "wifiscalediscovery.h"

#include <QHostInfo>
#include <QTimer>
#include <QDebug>

WifiScaleDiscovery::WifiScaleDiscovery(QObject* parent)
    : QObject(parent)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_lookupId == -1) return;
        qDebug() << "[WifiScaleDiscovery] mDNS lookup timed out for" << m_currentHostname;
        cancelInFlight();
        emit probeFinished();
    });
}

WifiScaleDiscovery::~WifiScaleDiscovery() {
    cancelInFlight();
}

void WifiScaleDiscovery::probe(const QString& hostname, int timeoutMs) {
    cancelInFlight();

    m_currentHostname = hostname;
    qDebug() << "[WifiScaleDiscovery] mDNS lookup for" << hostname
             << "(timeout" << timeoutMs << "ms)";

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
}

void WifiScaleDiscovery::cancelInFlight() {
    if (m_lookupId != -1) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    if (m_timeoutTimer) m_timeoutTimer->stop();
}
